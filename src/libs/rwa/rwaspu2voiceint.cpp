// rwaspu2voiceint.o (librwaspu2.a) -- the SPU2 voice interface definition and the hardware voice pool it hands out.
// Retail 0x2B2240-0x2B23A4 [C E map]. C port written from retail's code (audio1); every function is proven by the
// checker on live captures.
#include "rwa/rwacore.h"
#include "rwa/rwaspu2.h"

extern RwaObjInterfaceDef D_01E83340;              // the interface definition (static struct)
extern struct RwaUUID rwaSPU2VOICEINTERFACEID;
extern RwaObjInterfaceDefParam _rwaSPU2VoiceIntInputs[16];
extern RwaObjInterfaceDefParam _rwaSPU2VoiceIntOutputs[17];
extern RwaObjHandle **D_004E2C84;                  // the pool: handles of SPU2 voice objects [C 0x2B2378]
extern RwInt32 D_004E2C88;                         // its length [C 0x2B2380]
extern "C" RwaObjInterfaceDef *RwaObjInterfaceDefCreate(RwaObjInterfaceDef *def);
extern "C" RwBool RwaObjInterfaceDefDestroy(RwaObjInterfaceDef *def);
extern "C" RwaObjInterfaceDef *RwaObjInterfaceDefAssignID(RwaObjInterfaceDef *def, struct RwaUUID *id, const char *name);
extern "C" void *RwaObjInterfaceDefSetup(RwaObjInterfaceDef *def, RwaObjInterfaceDefParam *inputs, RwUInt32 nInputs,
                                         RwaObjInterfaceDefParam *outputs, RwUInt32 nOutputs);

// 0x002B2240
extern "C" RwaObjInterfaceDef *RwaSPU2VoiceInterfaceUnregister(void)
{
    return (RwaObjInterfaceDef *)RwaObjInterfaceDefDestroy(&D_01E83340);
}

// 0x002B2250
extern "C" RwaObjInterfaceDef *RwaSPU2VoiceInterfaceRegister(void)
{
    if (!RwaObjInterfaceDefCreate(&D_01E83340))
        return 0;
    RwaObjInterfaceDefAssignID(&D_01E83340, &rwaSPU2VOICEINTERFACEID, 0);
    RwaObjInterfaceDefSetup(&D_01E83340, _rwaSPU2VoiceIntInputs, 0x10, _rwaSPU2VoiceIntOutputs, 0x11);
    return &D_01E83340;
}

// Parameter getter (an accessor of the interface's output table): the first pool voice whose hardware state is 1 or 3
// [L: idle / finished] and not reserved; it becomes reserved (state Reserved = 1). *value = its handle, or null.
// 0x002B22C0
extern "C" RwaObj *func_002B22C0(RwaObj *obj, RwUInt32 paramId, RwaObjHandle **value)
{
    RwaObjHandle *h = 0;
    for (RwInt32 i = 0; i < D_004E2C88; i++) {
        RwaObjHandle *c = D_004E2C84[i];
        if (!c)
            continue;
        RwaSPU2VoiceState *st = (RwaSPU2VoiceState *)c->obj->state;
        RwInt32 play = st->StateDMA->PlayState;
        if (play != 3 && play != 1)
            continue;
        if (st->Reserved)
            continue;
        h = c;
        break;
    }
    if (h)
        ((RwaSPU2VoiceState *)h->obj->state)->Reserved = 1;
    *value = h;
    return obj;
}

// Parameter setter: installs the voice pool.
// 0x002B2370
extern "C" RwaObj *func_002B2370(RwaObj *obj, RwUInt32 paramId, RwaVoicePool *value)
{
    if (value) {
        D_004E2C84 = value->voices;
        D_004E2C88 = value->numVoices;
    } else {
        D_004E2C84 = 0;
        D_004E2C88 = 0;
    }
    return obj;
}
