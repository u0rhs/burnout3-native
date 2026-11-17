// rwaspu2outint.o (librwaspu2.a) -- the SPU2 output interface definition, and the SPU2 voice's parameter accessors
// (setters take the value or, for reals and integers, a pointer to it; getters write through the pointer; all return
// the object). Retail 0x2B0800-0x2B0CA4 [C E map; the accessors' object by position, L]. C port written from
// retail's code (audio1); proven by the checker on live captures where they exist (setters only were captured).
#include "rwa/rwacore.h"
#include "rwa/rwaspu2.h"

extern RwaObjInterfaceDef D_01E83290;      // the SPU2 output interface definition (static struct)
extern RwaUUID rwaSPU2OUTPUTINTERFACEID;
extern RwaObjInterfaceDefParam _rwaSPU2OutputIntInputs[7];
extern RwaObjInterfaceDefParam _rwaSPU2OutputIntOutputs[6];
extern "C" RwaObjInterfaceDef *RwaObjInterfaceDefCreate(RwaObjInterfaceDef *def);
extern "C" RwBool RwaObjInterfaceDefDestroy(RwaObjInterfaceDef *def);
extern "C" RwaObjInterfaceDef *RwaObjInterfaceDefAssignID(RwaObjInterfaceDef *def, RwaUUID *id, const char *name);
extern "C" void *RwaObjInterfaceDefSetup(RwaObjInterfaceDef *def, RwaObjInterfaceDefParam *inputs, RwUInt32 nInputs,
                                         RwaObjInterfaceDefParam *outputs, RwUInt32 nOutputs);

// 0x002B0800
extern "C" RwaObjInterfaceDef *RwaSPU2OutputInterfaceUnregister(void)
{
    return (RwaObjInterfaceDef *)RwaObjInterfaceDefDestroy(&D_01E83290);
}

// 0x002B0810
extern "C" RwaObjInterfaceDef *RwaSPU2OutputInterfaceRegister(void)
{
    if (!RwaObjInterfaceDefCreate(&D_01E83290))
        return 0;
    RwaObjInterfaceDefAssignID(&D_01E83290, &rwaSPU2OUTPUTINTERFACEID, 0);
    RwaObjInterfaceDefSetup(&D_01E83290, _rwaSPU2OutputIntInputs, 7, _rwaSPU2OutputIntOutputs, 6);
    return &D_01E83290;
}

// Gain array.
// 0x002B0890
extern "C" RwaSPU2Voice *func_002B0890(RwaSPU2Voice *v, RwUInt32 param, RwaGainArray *value)
{
    v->state.gainArray = value;
    return v;
}

// Reverb send gain (update 0x100).
// 0x002B08B0
extern "C" RwaSPU2Voice *func_002B08B0(RwaSPU2Voice *v, RwUInt32 param, RwReal *value)
{
    v->state.updateFlags |= 0x100;
    v->state.reverbSendGain = *value;
    return v;
}

// Pan, -1..1 -> 0..1 (update 4).
// 0x002B0900
extern "C" RwaSPU2Voice *func_002B0900(RwaSPU2Voice *v, RwUInt32 param, RwReal *value)
{
    v->state.Pan = *value * 0.5f + 0.5f;
    v->state.updateFlags |= 4;
    return v;
}

// Wave (as _rwaSPU2VoiceSetWave: a playing voice is stopped first).
// 0x002B0940
extern "C" RwaSPU2Voice *func_002B0940(RwaSPU2Voice *v, RwUInt32 param, RwaWave *wave)
{
    if (v->state.Wave)
        v->state.StateDMA->PlayState = 1;
    if (wave)
        v->state.Is3D = (wave->flags & 2) != 0;
    v->state.updateFlags |= 0x40;
    v->state.Wave = wave;
    return v;
}

// Loop.
// 0x002B09C0
extern "C" RwaSPU2Voice *func_002B09C0(RwaSPU2Voice *v, RwUInt32 param, RwBool *value)
{
    if (*value)
        v->state.StateDMA->Flags |= 1;
    else
        v->state.StateDMA->Flags &= ~1u;
    return v;
}

// Play position (the hardware's, as last reported).
// 0x002B0A00
extern "C" RwaSPU2Voice *func_002B0A00(RwaSPU2Voice *v, RwUInt32 param, RwUInt32 *value)
{
    *value = v->state.StateDMA->CurrentPosition;
    return v;
}

// Start position (update 0x80).
// 0x002B0A20
extern "C" RwaSPU2Voice *func_002B0A20(RwaSPU2Voice *v, RwUInt32 param, RwUInt32 *value)
{
    v->state.updateFlags |= 0x80;
    v->state.StateDMA->TriggerPosition = *value;
    return v;
}

// Playing: not in PlayState 1 (stopped) or 3 (idle).
// 0x002B0A40
extern "C" RwaSPU2Voice *func_002B0A40(RwaSPU2Voice *v, RwUInt32 param, RwBool *value)
{
    RwInt32 ps = v->state.StateDMA->PlayState;
    RwBool idle = ps == 3 || ps == 1;
    *value = !idle;
    return v;
}

// Play: key on (PlayState 2) or key off (1).
// 0x002B0A80
extern "C" RwaSPU2Voice *func_002B0A80(RwaSPU2Voice *v, RwUInt32 param, RwBool *value)
{
    v->state.StateDMA->PlayState = *value ? 2 : 1;
    return v;
}

// Volume (update 2).
// 0x002B0AB0
extern "C" RwaSPU2Voice *func_002B0AB0(RwaSPU2Voice *v, RwUInt32 param, RwReal *value)
{
    v->state.updateFlags |= 2;
    v->state.Volume = *value;
    return v;
}

// Frequency (update 1).
// 0x002B0AE0
extern "C" RwaSPU2Voice *func_002B0AE0(RwaSPU2Voice *v, RwUInt32 param, RwReal *value)
{
    v->state.updateFlags |= 1;
    v->state.Frequency = *value;
    return v;
}

// Max distance.
// 0x002B0B10
extern "C" RwaSPU2Voice *func_002B0B10(RwaSPU2Voice *v, RwUInt32 param, RwReal *value)
{
    v->state.MaxDistance3D = *value;
    return v;
}

// Min distance.
// 0x002B0B30
extern "C" RwaSPU2Voice *func_002B0B30(RwaSPU2Voice *v, RwUInt32 param, RwReal *value)
{
    v->state.MinDistance3D = *value;
    return v;
}

// Velocity.
// 0x002B0BE0
extern "C" RwaSPU2Voice *func_002B0BE0(RwaSPU2Voice *v, RwUInt32 param, RwV3d *value)
{
    v->state.Velocity3D = *value;
    return v;
}

// Position.
// 0x002B0C20
extern "C" RwaSPU2Voice *func_002B0C20(RwaSPU2Voice *v, RwUInt32 param, RwV3d *value)
{
    v->state.Position3D = *value;
    return v;
}
