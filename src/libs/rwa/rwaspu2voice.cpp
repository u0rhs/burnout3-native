// rwaspu2voice.o (librwaspu2.a) -- SPU2 (hardware) voices: per-update translation of a voice's state into its hardware
// voice registers (pitch, left/right volume, addresses, Dolby steering, reverb bits). Retail 0x2B0CB0-0x2B2234
// [C E map]. C port written from retail's code (audio1); every function is proven by the checker on live captures.
#include "rwa/rwacore.h"
#include "rwa/rwaspu2.h"
#include "rwa/rwavirtualvoice.h"

extern RwaObjDef D_01E832E0;               // the SPU2 voice object definition (static struct)
extern RwUInt8 D_01E832C0[0x20];           // its interface info block (static) [G size]
extern RwUInt32 D_004E2C68;                // _objInterfaceRegistered of rwaspu2voice.o [L name, retail_data_names.csv]
extern RwUInt8 _spu2voiceAliasInfo[8];
extern struct RwaUUID rwaSPU2VOICEID;
extern struct RwaUUID rwaSPU2OUTPUTID;
extern RwaObjInterfaceDefParam D_0049F730[16];  // input parameters
extern RwaObjInterfaceDefParam D_0049F8B0[17];  // output parameters
extern RwInt16 D_0049E280[256];            // square-root mantissa table [C 0x2B0DD8; research/retail_data_names.csv
                                           // calls it _spu2voiceInputParams, which this use contradicts]
extern RwReal D_004E06DC;                  // 0.8: speaker mode 3's pan width [C 0x2B0D5C]
extern RwReal D_004E0754;                  // 0.32612: Dolby pair front/back pan offset [C 0x2B1A88]
extern RwReal D_004E0758;                  // 0.34776: Dolby pair front/back pan slope [C 0x2B1A90]
extern RwReal _rwaFastCosTable[256];       // cos over a quarter turn, 0-255 [L: cos[0] = 1]
extern "C" RwUInt32 _rwaSPU2WaveRAMGetAddress(RwUInt32 handle);
extern "C" RwUInt32 _rwaSPU2WaveGetSamples(RwaWaveFormat *format);
extern "C" void _rwaListenerManagerCalculateVoiceAttributes(void *listener, void *voice, UnkStruct_002AE520_in *in,
                                                               UnkStruct_002AE520_out *out, RwUInt32 p4, RwUInt32 p5);
extern "C" RwReal RwV2dLength(RwV2d *v);
extern "C" RwReal RwV2dNormalize(RwV2d *out, RwV2d *in);
extern "C" RwaSPU2Voice *_RwaSPU2OutputFreeHWVoice(RwaSPU2Voice *v);
extern "C" RwaSPU2Voice *_RwaSPU2OutputAttachHWVoice(RwaSPU2Voice *v, RwUInt32 hwVoice);
extern "C" RwaSPU2Voice *_RwaSPU2OutputAllocateHWVoice(RwaSPU2Voice *v, RwaObj *output, RwaSPU2VoiceCreationParams *params);
extern "C" RwaObjDef *_rwaObjDefFindByUUID(RwaObjDef *start, struct RwaUUID *id, RwaObjDef **p2);
extern "C" RwaObjDef *RwaObjDefCreate(RwaObjDef *parent, RwUInt32 p1, RwaObjDef *def, void *intfInfo, void *aliasInfo,
                                      RwUInt32 hint);
extern "C" RwaObjDef *RwaObjDefAssignID(RwaObjDef *def, struct RwaUUID *id, const char *name);
extern "C" RwaObjDef *RwaObjDefSetupParams(RwaObjDef *def, RwaObjInterfaceDefParam *inputs, RwUInt32 nInputs,
                                       RwaObjInterfaceDefParam *outputs, RwUInt32 nOutputs);
extern "C" RwaObjDef *RwaObjDefSetInterface(RwaObjDef *def, RwaObjInterfaceDef *intf);
extern "C" RwaObjDef *RwaObjDefDestroy(RwaObjDef *def);
extern "C" RwaObjInterfaceDef *RwaSPU2VoiceInterfaceRegister(void);
extern "C" RwaObjInterfaceDef *RwaSPU2VoiceInterfaceUnregister(void);
extern "C" RwaObj *RwaObjCreate(RwaObjDef *def, RwaObj *p1, RwUInt32 p2, void *p3, RwUInt32 p4, RwUInt32 p5);

// RenderWare's table square root on the float's bits: exponent halved, mantissa's top 8 bits (with the odd-exponent
// bit) through D_0049E280 [C 0x2B0D94-0x2B0DF0]. The caller skips it for 0.
static RwReal func_002B0CB0_inl1(RwReal x)
{
    union {
        RwReal f;
        RwUInt32 u;
    } v;
    v.f = x;
    RwInt16 e = (RwInt16)((v.u >> 23) - 0x7F);
    RwUInt32 m = v.u & 0x7FFFFF;
    if (e & 1)
        m |= 0x800000;
    v.u = ((RwUInt32)((e >> 1) + 0x7F) << 23) | ((RwUInt32)(RwInt32)D_0049E280[m >> 16] << 16);
    return v.f;
}

// Volumes register from the gains: (int)(0x7FFF * a * scale) << 16 | (int)(0x7FFF * b * scale).
static RwUInt32 func_002B0CB0_inl2(RwReal a, RwReal b, RwReal scale)
{
    RwReal fa = a * scale;
    RwReal fb = b * scale;
    RwReal full = (RwReal)0x7FFF;
    fa = full * fa;
    fb = full * fb;
    return ((RwUInt32)(RwInt32)fa << 16) | (RwUInt32)(RwInt32)fb;
}

// Left/right volume: with a Dolby mode (0x08/0x10) the steering gains VolL/VolR; else an equal-power pan law,
// left = sqrt(1 - p), right = sqrt(p), p = 0.5 + (pan - 0.5) * width (width 0 in speaker mode 2, 0.8 in mode 3, else
// 1; pan = Pan3D for a 3D voice, else Pan); both times Volume * VolumeScale3D. A Dolby pair (0x20) is left alone.
// 0x002B0CB0
extern "C" void _RwaSPU2VoiceSetLRVol(RwaSPU2Voice *v)
{
    RwaSPU2VoiceState *st = (RwaSPU2VoiceState *)v->obj.state;
    RwReal scale = st->Volume * st->VolumeScale3D;
    RwaSPU2VoiceStateDMA *dma = st->StateDMA;
    RwUInt32 dolby = st->DolbyState;
    if (dolby & 0x18) {
        dma->Volumes = func_002B0CB0_inl2(st->VolL, st->VolR, scale);
        return;
    }
    if (dolby & 0x20)
        return;
    RwReal pan = st->Is3D ? st->Pan3D : st->Pan;
    RwUInt32 mode = ((RwaSPU2OutputState *)st->Output->state)->DMAState.SpeakerConfig;
    pan = pan - 0.5f;
    if (mode == 5 || mode == 4 || mode == 1) {
    } else if (mode == 3) {
        pan = pan * D_004E06DC;
    } else if (mode == 2) {
        pan = pan * 0.0f;
    }
    RwReal r = pan + 0.5f;
    RwReal l = 1.0f - r;
    if (!(l == 0.0f))
        l = func_002B0CB0_inl1(l);
    if (!(r == 0.0f))
        r = func_002B0CB0_inl1(r);
    dma->Volumes = func_002B0CB0_inl2(l, r, scale);
}

// 0x002B0F20
extern "C" void _rwaSPU2VoiceSetPan(RwaSPU2Voice *v, RwReal pan)
{
    v->state.Pan = pan * 0.5f + 0.5f;
    v->state.updateFlags |= 4;
}

// A new wave stops a voice that has one (PlayState 1).
// 0x002B0F50
extern "C" void _rwaSPU2VoiceSetWave(RwaSPU2Voice *v, RwaWave *wave)
{
    if (v->state.Wave)
        v->state.StateDMA->PlayState = 1;
    if (wave)
        v->state.Is3D = (wave->flags & 2) != 0;
    v->state.updateFlags |= 0x40;
    v->state.Wave = wave;
}

// 0x002B0FA0
extern "C" void _rwaSPU2VoiceSetLoop(RwaSPU2Voice *v, RwBool loop)
{
    if (loop)
        v->state.StateDMA->Flags |= 1;
    else
        v->state.StateDMA->Flags &= ~1u;
}

// Creation-parameter info: {0x17, 0x30} [G meaning].
// 0x002B0FE0
extern "C" RwaObj *func_002B0FE0(RwaObj *obj, RwUInt32 *out)
{
    out[0] = 0x17;
    out[1] = 0x30;
    return obj;
}

// The Dolby partner mirrors the voice, its addresses 0x80 bytes on (one ADPCM block of 0x10 bytes is 28 samples;
// samples here are the wave's); a wave shorter than 0x8D samples shares the voice's addresses [C 0x2B1350-0x2B142C].
static void func_002B1000_inl1(RwaSPU2VoiceState *st, RwaSPU2VoiceStateDMA *dma)
{
    RwaSPU2VoiceStateDMA *p = st->DolbyPartner;
    RwUInt32 n = _rwaSPU2WaveGetSamples(&st->Wave->targetFormat);
    p->PlayState = dma->PlayState;
    p->Frequency = dma->Frequency;
    p->Volumes = dma->Volumes;
    p->Flags = dma->Flags;
    if (n < 0x8D) {
        p->StartAddress = dma->StartAddress;
        p->LoopStartAddress = dma->LoopStartAddress;
        p->CurrentPosition = dma->CurrentPosition;
        p->TriggerPosition = dma->TriggerPosition;
        return;
    }
    p->StartAddress = dma->StartAddress + 0x80;
    p->LoopStartAddress = dma->LoopStartAddress + 0x80;
    RwUInt32 last = n - 0x8C;
    if (last < dma->TriggerPosition)
        dma->TriggerPosition = last;
    p->TriggerPosition = dma->TriggerPosition + 0x80;
    p->CurrentPosition = dma->CurrentPosition + 0x80;
    if (!(p->CurrentPosition < n)) {
        if (dma->Flags & 1)
            p->CurrentPosition = p->CurrentPosition - n;
        else
            p->CurrentPosition = n - 0x1C;
    }
}

// The voice's reverb bit in its core's two masks.
static void func_002B1000_inl2(RwaSPU2VoiceState *st, RwaSPU2OutputState *out, RwBool on)
{
    RwUInt32 core = st->voiceNum & 1;
    if (on) {
        out->DMAState.outputMixWetLeft[core] |= st->voiceBit;
        out->DMAState.outputMixWetRight[core] |= st->voiceBit;
    } else {
        out->DMAState.outputMixWetLeft[core] &= ~st->voiceBit;
        out->DMAState.outputMixWetRight[core] &= ~st->voiceBit;
    }
}

// Per update, after the voice's parameters changed: pending updates into the hardware registers; then, for a 3D voice
// that is playing or one with a gain array, the listener's attributes (volume scale, Doppler, pan) or the gain array's
// speaker gains, and the volumes again.
// 0x002B1000
extern "C" RwaSPU2Voice *_RwaSPU2VoiceFinishExecute(RwaSPU2Voice *v)
{
    RwaSPU2VoiceState *st = &v->state;
    RwaSPU2VoiceStateDMA *dma = st->StateDMA;
    RwaSPU2OutputState *out = (RwaSPU2OutputState *)st->Output->state;
    if (v->state.updateFlags) {
        RwUInt32 upd = v->state.updateFlags;
        RwaWave *wave = st->Wave;
        RwaWaveFormat *fmt = &wave->targetFormat;
        if (!(st->mode & 0x40) && (upd & 0x40) && wave) {
            // a new wave: addresses, trigger, frequency, defaults
            RwUInt32 addr = _rwaSPU2WaveRAMGetAddress(wave->data);
            st->StateDMA->StartAddress = addr;
            if (!(upd & 0x200))
                st->StateDMA->LoopStartAddress = addr;
            if (!(upd & 0x80))
                st->StateDMA->TriggerPosition = 0;
            st->StateDMA->CurrentPosition = st->StateDMA->TriggerPosition;
            if (!(upd & 1)) {
                st->Frequency = (RwReal)fmt->sampleRate;
                upd |= 1;
            }
            st->FrequencyScale3D = 1.0f;
            st->VolumeScale3D = 1.0f;
            st->Pan3D = 0.5f;
            st->Is3D = (wave->flags & 2) != 0;
            if (!st->Is3D) {
                st->VolL = 1.0f;
                st->VolR = 1.0f;
                st->MinDistance3D = 1.0f;
                st->MaxDistance3D = (RwReal)10000;
                st->DolbyState = 0;
                v->state.updateFlags |= 1;
                v->state.Frequency = st->Frequency;
                v->state.updateFlags |= 2;
                v->state.Volume = st->Volume;
                if (!(upd & 4)) {
                    RwReal half = 0.5f;
                    v->state.Pan = half * half + half;
                    v->state.updateFlags |= 4;
                }
            }
            if (st->Is3D && out->DMAState.SpeakerConfig >= 4 && st->DolbyDesired) {
                st->DolbyState = 0x10;
                if ((st->DolbyDesired & 0x20) && st->DolbyPartner) {
                    st->DolbyState = 0x20;
                    st->DolbyPartner->Flags &= ~6u;
                }
            } else {
                st->DolbyState = 0;
            }
            dma->Flags &= ~6u;
        }
        if (upd & 0x200)
            st->StateDMA->LoopStartAddress = st->StateDMA->LoopStartAddress + st->StateDMA->StartAddress;
        if (upd & 0x100)
            func_002B1000_inl2(st, out, !(st->reverbSendGain <= 0.0f));
        if (upd & 1)
            st->StateDMA->Frequency = (RwInt32)(st->Frequency * st->FrequencyScale3D);
        if (upd & 6)
            _RwaSPU2VoiceSetLRVol(v);
        if (st->DolbyState == 0x20)
            func_002B1000_inl1(st, dma);
        st->updateFlags = 0;
    }
    st->Reserved = 0;
    if (dma->PlayState == 3)
        return v;
    RwBool spatial = 1;
    if (dma->PlayState == 1 || !st->Is3D)
        spatial = 0;
    if (!spatial && !st->gainArray)
        return v;

    UnkStruct_002AE520_in in;
    UnkStruct_002AE520_out attr;
    in.field_0 = st->Position3D;
    in.field_C = st->Velocity3D;
    in.field_18 = st->MinDistance3D;
    in.field_1C = st->MaxDistance3D;
    _rwaListenerManagerCalculateVoiceAttributes(out->Listener, v, &in, &attr, out->DMAState.SpeakerConfig, 7);
    st->ListenerRelativePosition3D = in.field_0;
    st->ListenerRelativeVelocity3D = in.field_C;
    // attr: field_0 volume scale, field_4[0] frequency scale, field_4[2] pan x (left-right), field_4[3] pan y
    // (front-back) [C 0x2B16D4-0x2B16F4, 0x2B1840]
    RwaGainArray *ga = st->gainArray;
    if (ga) {
        RwaPatchbay *pb = ga->patchbay;
        RwReal centre = 0.0f;
        RwV2d pan;
        pan.x = 0.0f;
        pan.y = 0.0f;
        RwUInt32 n = pb->numConnections;
        RwaConnection *c = pb->connections;
        for (RwUInt32 i = 0; i <= n; i++) {
            switch (c[i].outputSocketNumber) {
            case 1:
                pan.x = pan.x - ga->gainValues[i];
                pan.y = pan.y + ga->gainValues[i];
                break;
            case 3:
                pan.x = pan.x - ga->gainValues[i];
                pan.y = pan.y - ga->gainValues[i];
                break;
            case 2:
                pan.x = pan.x + ga->gainValues[i];
                pan.y = pan.y + ga->gainValues[i];
                break;
            case 4:
                pan.x = pan.x + ga->gainValues[i];
                pan.y = pan.y - ga->gainValues[i];
                break;
            case 5:
                centre = ga->gainValues[i];
                break;
            case 7:
                func_002B1000_inl2(st, out, !(ga->gainValues[i] <= 0.0f));
                break;
            default:
                break;
            }
        }
        if (pb->overridePositional) {
            RwReal len = RwV2dLength(&pan);
            attr.field_0 = len;
            if (!(len <= 1.0f))
                attr.field_0 = 1.0f;
            if (st->DolbyState) {
                if (!(attr.field_0 <= 0.0f)) {
                    RwV2dNormalize(&pan, &pan);
                    pan.x = pan.x * 0.5f;
                    pan.y = pan.y * 0.5f;
                }
            } else {
                RwReal x = pan.x * 0.5f;
                pan.x = x;
                if (!(x <= 0.5f))
                    pan.x = 0.5f;
                else if (x < -0.5f)
                    pan.x = -0.5f;
                pan.y = 0.0f;
            }
            pan.x = pan.x + 0.5f;
            pan.y = 0.5f - pan.y;
        }
        if (!(centre <= 0.0f)) {
            pan.y = 0.0f;
            pan.x = 0.5f;
            attr.field_0 = centre;
        }
        attr.field_4[3] = pan.y;
        attr.field_4[2] = pan.x;
    }
    st->VolumeScale3D = attr.field_0;
    if (st->mode & 4)
        st->FrequencyScale3D = attr.field_4[0];
    st->Pan3D = attr.field_4[2];
    if (st->DolbyState) {
        RwReal full = 255.0f;
        RwReal oldY = st->OldYPan;
        RwReal y = attr.field_4[3];
        RwInt32 iy = (RwInt32)(full * y);
        st->OldYPan = y;
        RwInt32 ix = (RwInt32)(full * attr.field_4[2]);
        RwInt32 ioldY = (RwInt32)(full * oldY);
        if (!(st->DolbyState & 0x20)) {
            // one voice: front/back by phase inversion of one side
            RwBool toRear = 0, toFront = 0;
            if (iy < 0x80) {
                st->VolL = _rwaFastCosTable[ix] * _rwaFastCosTable[iy];
                st->VolR = _rwaFastCosTable[0xFF - ix] * _rwaFastCosTable[iy];
            } else {
                RwReal cy = _rwaFastCosTable[0xFF - iy];
                st->VolL = _rwaFastCosTable[ix] * cy;
                st->VolR = _rwaFastCosTable[0xFF - ix] * cy;
            }
            RwUInt32 fl = dma->Flags;
            RwUInt32 inv = fl & 6;
            if (inv == 4 || inv == 2)
                inv = 1;
            if (ioldY < 0x80) {
                if (iy < 0x80) {
                    if (inv)
                        toFront = 1;
                } else {
                    toRear = 1;
                }
            } else {
                if (iy < 0x80)
                    toFront = 1;
                else if (!inv)
                    toRear = 1;
            }
            if (toRear) {
                if (st->VolL < st->VolR) {
                    if (fl & 2)
                        dma->Flags &= ~4u;
                    else
                        dma->Flags |= 4;
                } else {
                    if (fl & 4)
                        dma->Flags &= ~2u;
                    else
                        dma->Flags |= 2;
                }
            } else if (toFront) {
                if (st->VolL < st->VolR) {
                    if (fl & 2)
                        dma->Flags |= 4;
                    else
                        dma->Flags &= ~4u;
                } else {
                    if (fl & 4)
                        dma->Flags |= 2;
                    else
                        dma->Flags &= ~2u;
                }
            }
        } else {
            // a voice pair: the partner plays the rear part, phase-inverted on the left
            RwReal x = attr.field_4[2];
            RwReal py = D_004E0754 + D_004E0758 * x;
            RwInt32 ip = (RwInt32)(full * py);
            RwReal cy = _rwaFastCosTable[iy];
            st->VolL = _rwaFastCosTable[ix] * cy;
            st->VolR = _rwaFastCosTable[0xFF - ix] * cy;
            RwaSPU2VoiceStateDMA *p = st->DolbyPartner;
            dma->Frequency = (RwInt32)(st->Frequency * st->FrequencyScale3D);
            p->Frequency = dma->Frequency;
            RwReal scale = st->Volume * st->VolumeScale3D;
            dma->Volumes = func_002B0CB0_inl2(st->VolL, st->VolR, scale);
            RwReal cby = _rwaFastCosTable[0xFF - iy];
            RwReal lo = -_rwaFastCosTable[0xFF - ip] * cby;
            RwReal hi = _rwaFastCosTable[ip] * cby;
            p->Volumes = func_002B0CB0_inl2(hi, lo, scale);
        }
    }
    if (st->DolbyState & 0x20)
        return v;
    v->state.updateFlags |= 1;
    v->state.Frequency = st->Frequency;
    _RwaSPU2VoiceSetLRVol(v);
    return v;
}

// Destroy: drops the gain array, frees the hardware voice.
// 0x002B1C00
extern "C" RwUInt32 func_002B1C00(RwaSPU2Voice *v)
{
    v->state.gainArray = 0;
    return (RwUInt32)_RwaSPU2OutputFreeHWVoice(v);
}

// Create: binds a hardware voice (a fixed one with mode 0x40: 0x20 becomes 0x10 then), then the defaults.
// 0x002B1C10
extern "C" RwaSPU2Voice *func_002B1C10(RwaSPU2Voice *v, RwaObj *output, RwaSPU2VoiceCreationParams *params)
{
    RwaSPU2VoiceState *st = &v->state;
    v->state.Output = output;
    v->state.mode = params->mode;
    v->state.DolbyPartner = 0;
    if (v->state.mode & 0x40) {
        if (v->state.mode & 0x20) {
            st->mode &= ~0x20u;
            st->mode |= 0x10;
        }
        if (!_RwaSPU2OutputAttachHWVoice(v, params->voice))
            return 0;
    } else if (!_RwaSPU2OutputAllocateHWVoice(v, output, params)) {
        return 0;
    }
    st->DolbyDesired = st->mode & 0x38;
    st->Frequency = (RwReal)22050;
    st->Volume = 1.0f;
    st->reverbSendGain = 1.0f;
    st->Pan = 0.5f;
    st->FrequencyScale3D = 1.0f;
    st->VolumeScale3D = 1.0f;
    st->Pan3D = 0.5f;
    st->VolL = 1.0f;
    st->VolR = 1.0f;
    st->MinDistance3D = 1.0f;
    st->MaxDistance3D = (RwReal)10000;
    st->Is3D = (st->mode & 0x80) != 0;
    st->DolbyState = 0;
    st->OldYPan = 0;
    st->Reserved = 0;
    st->updateFlags = 0;
    st->Wave = 0;
    st->gainArray = 0;
    RwV3d zero;
    zero.x = 0.0f;
    zero.y = 0.0f;
    zero.z = 0.0f;
    st->Position3D = zero;
    st->Velocity3D = zero;
    st->ListenerRelativePosition3D = zero;
    st->ListenerRelativeVelocity3D = zero;
    st->StateDMA->StartAddress = 0;
    st->StateDMA->LoopStartAddress = 0;
    st->StateDMA->PlayState = (st->mode & 0x40) ? 5 : 3;
    st->StateDMA->Frequency = 22050;
    st->StateDMA->Volumes = 0x40004000;
    st->StateDMA->CurrentPosition = 0;
    st->StateDMA->TriggerPosition = 0;
    st->StateDMA->Flags = 0;
    return v;
}

// 0x002B1E00
extern "C" RwaObjDef *RwaSPU2VoiceUnregister(void)
{
    if (D_004E2C68) {
        if (!RwaSPU2VoiceInterfaceUnregister())
            return 0;
        D_004E2C68 = 0;
    }
    return RwaObjDefDestroy(&D_01E832E0);
}

// 0x002B1E50
extern "C" void _RwaSPU2VoiceRegisterCommon(RwaObjDef *def)
{
    RwaObjDefAssignID(def, &rwaSPU2VOICEID, 0);
    def->noFreeListEntries = 8;
    func_00297C30_inl3(def, 0x94, 0x30);
    def->createFunc = func_002B1C10;
    def->destroyFunc = func_002B1C00;
    def->startExecuteFunc = 0;
    def->finishExecuteFunc = _RwaSPU2VoiceFinishExecute;
    def->getParamsFunc = func_002B0FE0;
    def->createParamSize = 8;
}

// 0x002B2130
extern "C" RwaObjDef *RwaSPU2VoiceRegisterUsingInterface(void)
{
    RwaObjDef *parent = _rwaObjDefFindByUUID(0, &rwaSPU2OUTPUTID, 0);
    RwaObjDef *def = RwaObjDefCreate(parent, 0xC, &D_01E832E0, D_01E832C0, _spu2voiceAliasInfo, 0x3080E);
    if (!def)
        return 0;
    _RwaSPU2VoiceRegisterCommon(def);
    if (!RwaObjDefSetupParams(def, D_0049F730, 0x10, D_0049F8B0, 0x11)) {
        RwaObjDefDestroy(def);
        return 0;
    }
    RwaObjInterfaceDef *intf = RwaSPU2VoiceInterfaceRegister();
    if (!intf) {
        RwaObjDefDestroy(def);
        return 0;
    }
    RwaObjDefSetInterface(def, intf);
    D_004E2C68 = 1;
    return def;
}

// 0x002B2210
extern "C" RwaObj *RwaSPU2VoiceCreate(RwUInt32 p0, RwUInt32 p1, RwUInt32 p2, RwUInt32 p3, RwUInt32 p4)
{
    return RwaObjCreate(&D_01E832E0, (RwaObj *)p0, p2, (void *)p1, p3, p4);
}
