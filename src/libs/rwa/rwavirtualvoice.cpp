// rwavirtualvoice.o (librwacore.a) -- virtual voices: the game's voices, which the voice manager (rwavoicemanager.o)
// backs with real (hardware) voices by weight. Retail 0x294DA0-0x294E1C and 0x296F20-0x29806C [C E map].
// C port written from retail's code (audio1); every function is proven by the checker on live captures.
#include "rwa/rwacore.h"
#include "rwa/rwavirtualvoice.h"

extern RwaObjInterfaceDef D_01E7D040;      // the virtual voice interface definition (static struct)
extern RwaObjInterfaceDef D_01E7D1E0;      // the 3D voice interface definition (static struct)
extern RwaObjDef D_01E7D180;               // the virtual voice object definition (static struct)
extern RwaObjDefInterfaceInfo D_01E7D160;  // its interface info (static) [G type: 0x18 of 0x20 bytes]
extern RwLinkList _virtualvoiceAliasInfo;
extern RwUInt32 D_004E2B58;                // _objInterfaceRegistered of rwavirtualvoice.o [L name, retail_data_names.csv]
extern RwaObjDef *_rwaGnullObjDef;
extern RwaUUID rwaVIRTUALVOICEINTERFACEID;
extern RwaUUID rwaVOICE3DINTERFACEID;
extern RwaUUID rwaVIRTUALVOICEID;
extern RwaObjInterfaceDefParam _rwaVirtualVoiceIntInputs[17];
extern RwaObjInterfaceDefParam _rwaVirtualVoiceIntOutputs[18];
extern RwaObjInterfaceDefParam D_0049DD00[15];      // the 3D voice interface's inputs
extern RwaObjInterfaceDefParam D_0049DE30[16];      // its outputs
extern RwaInputParamDef _virtualvoiceInputParams[17];
extern RwaOutputParamDef _virtualvoiceOutputParams[18];
extern RwUInt32 _rwaVoiceManagerTimeStamp;
extern "C" void *memset(void *dst, int c, unsigned int n);
extern "C" RwaObjInterfaceDef *RwaObjInterfaceDefCreate(RwaObjInterfaceDef *def);
extern "C" RwBool RwaObjInterfaceDefDestroy(RwaObjInterfaceDef *def);
extern "C" RwaObjInterfaceDef *RwaObjInterfaceDefAssignID(RwaObjInterfaceDef *def, RwaUUID *id, const char *name);
extern "C" void *RwaObjInterfaceDefSetup(RwaObjInterfaceDef *def, RwaObjInterfaceDefParam *inputs, RwUInt32 nInputs,
                                         RwaObjInterfaceDefParam *outputs, RwUInt32 nOutputs);
extern "C" RwaObjDef *RwaObjDefCreate(RwaObjDef *parent, RwUInt32 p1, RwaObjDef *def, void *intfInfo, void *aliasInfo,
                                      RwUInt32 hint);
extern "C" RwaObjDef *RwaObjDefAssignID(RwaObjDef *def, RwaUUID *id, const char *name);
extern "C" RwaObjDef *RwaObjDefSetupParams(RwaObjDef *def, RwaInputParamDef *inputs, RwUInt32 nInputs,
                                       RwaOutputParamDef *outputs, RwUInt32 nOutputs);
extern "C" RwaObjDef *RwaObjDefSetInterface(RwaObjDef *def, RwaObjInterfaceDef *intf);
extern "C" RwaObjDef *RwaObjDefDestroy(RwaObjDef *def);
extern "C" RwaObj *RwaObjCreate(RwaObjDef *def, RwaObj *p1, RwUInt32 p2, void *p3, RwUInt32 p4, RwUInt32 p5);
extern "C" RwInt32 RwaObjHandleGetParamInt32(RwaObjHandle *handle, RwUInt32 param, RwUInt32 type, RwBool *ok);
extern "C" RwaObjHandle *RwaObjHandleSetParamPointer(RwaObjHandle *handle, RwUInt32 param, RwUInt32 type, void *value);
extern "C" void RwaVoiceManagerUpdateVoice(RwaVirtualVoiceState *v, RwUInt32 mode);
extern "C" RwBool RwaVoiceManagerRegisterVoice(RwaVirtualVoice *vv);

// 0x00294DA0
extern "C" RwaObjInterfaceDef *RwaVirtualVoiceInterfaceUnregister(void)
{
    return (RwaObjInterfaceDef *)RwaObjInterfaceDefDestroy(&D_01E7D040);
}

// 0x00294DB0
extern "C" RwaObjInterfaceDef *RwaVirtualVoiceInterfaceRegister(void)
{
    if (!RwaObjInterfaceDefCreate(&D_01E7D040))
        return 0;
    RwaObjInterfaceDefAssignID(&D_01E7D040, &rwaVIRTUALVOICEINTERFACEID, 0);
    RwaObjInterfaceDefSetup(&D_01E7D040, _rwaVirtualVoiceIntInputs, 0x11, _rwaVirtualVoiceIntOutputs, 0x12);
    return &D_01E7D040;
}

// The wave; frequency = its played sample rate unless a new wave is pending (flag 0x20); the real voice, if any, gets
// it at once (parameter 0xB).
// 0x00297930
extern "C" void _rwaVirtualVoiceSetWave(RwaVirtualVoice *vv, RwaWave *wave)
{
    RwaVirtualVoiceState *v = &vv->state;
    if (v->Wave)
        v->Wave = 0;
    if (wave)
        v->Wave = wave;
    if (wave && !(v->Flags & 0x20))
        v->Frequency = (RwReal)wave->targetFormat.sampleRate;
    if (v->RealVoice)
        RwaObjHandleSetParamPointer(v->RealVoice, 0xB, 1, v->Wave);
}

// Trigger: start (with the time stamp as the position's time) or stop.
// 0x002979E0
extern "C" void _rwaVirtualVoiceSetTrigger(RwaVirtualVoice *vv, RwBool on)
{
    if (!on) {
        RwaVoiceManagerUpdateVoice(&vv->state, 1);
        return;
    }
    RwaVoiceManagerUpdateVoice(&vv->state, 0);
    vv->state.LastPosTime = _rwaVoiceManagerTimeStamp;
}

// Creation-parameter info: none.
// 0x00297A30
extern "C" RwaObj *func_00297A30(RwaObj *obj, RwUInt32 *out)
{
    out[0] = 0;
    return obj;
}

// Per update: a started voice is re-sorted (its weight may have changed), or stopped when its real voice stopped
// playing (real voice parameter 8 reads 0).
// 0x00297A40
extern "C" RwaVirtualVoice *_RwaVirtualVoiceStartExecute(RwaVirtualVoice *vv)
{
    RwaVirtualVoiceState *v = &vv->state;
    if (vv->state.Flags & 2) {
        RwUInt32 mode = 2;
        if (v->RealVoice) {
            if (!RwaObjHandleGetParamInt32(v->RealVoice, 8, 2, 0))
                mode = 1;
        }
        RwaVoiceManagerUpdateVoice(v, mode);
    }
    v->Flags &= 0xDF;
    return vv;
}

// 0x00297AD0
extern "C" void _RwaVirtualVoiceDestroy(RwaVirtualVoice *vv)
{
    RwaVoiceManagerUpdateVoice(&vv->state, 1);
    if (vv->state.link.next) {
        vv->state.link.prev->next = vv->state.link.next;
        vv->state.link.next->prev = vv->state.link.prev;
    }
}

// Creation: defaults, into the voice manager's free list. params->field_0 & 0x18 become the flags.
// 0x00297B20
extern "C" RwaVirtualVoice *_RwaVirtualVoiceCreate(RwaVirtualVoice *vv, RwaObj *p1, RwUInt32 *params)
{
    RwaVirtualVoiceState *v = &vv->state;
    memset(v, 0, 0x64);
    v->link.prev = 0;
    v->link.next = 0;
    RwaVoiceManagerRegisterVoice(vv);
    v->Flags = params[0] & 0x18;
    v->Frequency = (RwReal)22050;
    v->Gain = 0xFF;
    v->Pan = 0;
    v->MinDistance = 1.0f;
    v->MaxDistance = (RwReal)10000;
    v->LastPosTime = 0;
    v->WavePosition = 0;
    v->WaveStartPosition = 0;
    v->Priority = 0x7F;
    v->Bias = 0x7F;
    v->Weight = 0x7F;
    v->RealVoice = 0;
    return vv;
}

// 0x00297BE0
extern "C" RwaObjDef *RwaVirtualVoiceUnregister(void)
{
    if (D_004E2B58) {
        if (!RwaVirtualVoiceInterfaceUnregister())
            return 0;
        D_004E2B58 = 0;
    }
    return RwaObjDefDestroy(&D_01E7D180);
}

// 0x00297C30
extern "C" void _RwaVirtualVoiceRegisterCommon(RwaObjDef *def)
{
    RwaObjDefAssignID(def, &rwaVIRTUALVOICEID, 0);
    func_00297C30_inl3(def, 0x64, 0x28);
    def->createFunc = _RwaVirtualVoiceCreate;
    def->destroyFunc = _RwaVirtualVoiceDestroy;
    def->startExecuteFunc = _RwaVirtualVoiceStartExecute;
    def->getParamsFunc = func_00297A30;
    def->createParamSize = 4;
}

// 0x00297F00
extern "C" RwaObjDef *RwaVirtualVoiceRegisterUsingInterface(void)
{
    RwaObjDef *def = RwaObjDefCreate(_rwaGnullObjDef, 0xC, &D_01E7D180, &D_01E7D160, &_virtualvoiceAliasInfo, 0x30811);
    if (!def)
        return 0;
    _RwaVirtualVoiceRegisterCommon(def);
    if (!RwaObjDefSetupParams(def, _virtualvoiceInputParams, 0x11, _virtualvoiceOutputParams, 0x12)) {
        RwaObjDefDestroy(def);
        return 0;
    }
    RwaObjInterfaceDef *intf = RwaVirtualVoiceInterfaceRegister();
    if (!intf) {
        RwaObjDefDestroy(def);
        return 0;
    }
    RwaObjDefSetInterface(def, intf);
    D_004E2B58 = 1;
    return def;
}

// 0x00297FD0
extern "C" RwaObj *RwaVirtualVoiceCreate(RwUInt32 p0, RwUInt32 p1, RwUInt32 p2, RwUInt32 p3, RwUInt32 p4)
{
    return RwaObjCreate(&D_01E7D180, (RwaObj *)p0, p2, (void *)p1, p3, p4);
}

// The 3D voice interface (rwaVOICE3DINTERFACEID) definition.
// 0x00298000
extern "C" RwaObjInterfaceDef *func_00298000(void)
{
    if (!RwaObjInterfaceDefCreate(&D_01E7D1E0))
        return 0;
    RwaObjInterfaceDefAssignID(&D_01E7D1E0, &rwaVOICE3DINTERFACEID, 0);
    RwaObjInterfaceDefSetup(&D_01E7D1E0, D_0049DD00, 0xF, D_0049DE30, 0x10);
    return &D_01E7D1E0;
}
