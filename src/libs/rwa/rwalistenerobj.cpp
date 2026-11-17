// rwalistenerobj.o (librwacore.a) -- the listener object. Retail 0x298420-0x298D9C [C E map]. C port written from
// retail's code (audio1); every function is proven by the checker on live captures. _rwaListenerCalculateVoiceAttributes
// (0x298420) is not ported yet (never captured).
#include "rwa/rwacore.h"
#include "rwa/rwalistener.h"

extern RwaObjDef D_01E7D260;               // the listener object definition (static struct)
extern RwaObjDefInterfaceInfo D_01E7D240;  // its interface info (static) [G type]
extern RwLinkList _listenerAliasInfo;
extern RwUInt32 D_004E2B68;                // _objInterfaceRegistered of rwalistenerobj.o [L name, retail_data_names.csv]
extern RwaObjDef *_rwaGnullObjDef;
extern RwaUUID rwaLISTENERID;
extern RwaInputParamDef _listenerInputParams[8];
extern RwaOutputParamDef _listenerOutputParams[9];
extern "C" RwaObjDef *RwaObjDefCreate(RwaObjDef *parent, RwUInt32 p1, RwaObjDef *def, void *intfInfo, void *aliasInfo,
                                      RwUInt32 hint);
extern "C" RwaObjDef *RwaObjDefAssignID(RwaObjDef *def, RwaUUID *id, const char *name);
extern "C" RwaObjDef *RwaObjDefSetupParams(RwaObjDef *def, RwaInputParamDef *inputs, RwUInt32 nInputs,
                                       RwaOutputParamDef *outputs, RwUInt32 nOutputs);
extern "C" RwaObjDef *RwaObjDefSetInterface(RwaObjDef *def, RwaObjInterfaceDef *intf);
extern "C" RwaObjDef *RwaObjDefDestroy(RwaObjDef *def);
extern "C" RwaObj *RwaObjCreate(RwaObjDef *def, RwaObj *p1, RwUInt32 p2, void *p3, RwUInt32 p4, RwUInt32 p5);
extern "C" RwaObjInterfaceDef *RwaListenerInterfaceRegister(void);

// Creation: at the origin, at rest, facing +z with +y up; rolloff 10, Doppler and distance factors 1.
// 0x002988E0
extern "C" RwaListener *_RwaListenerCreate(RwaListener *l)
{
    RwaListenerState *s = &l->state;
    RwV3d zero;
    zero.x = 0.0f;
    zero.y = 0.0f;
    zero.z = 0.0f;
    s->flags = 0;
    s->Position = zero;
    s->flags |= 1;
    s->Velocity = zero;
    s->flags |= 2;
    s->At.x = 0.0f;
    s->At.y = 0.0f;
    s->At.z = 1.0f;
    s->Up.x = 0.0f;
    s->Up.y = 1.0f;
    s->Up.z = 0.0f;
    s->Right.x = s->Up.y * s->At.z - s->Up.z * s->At.y;
    s->Right.y = s->Up.z * s->At.x - s->Up.x * s->At.z;
    s->Right.z = s->Up.x * s->At.y - s->Up.y * s->At.x;
    s->flags |= 8;
    s->DopplerScale = 1.0f;
    s->flags |= 0x20;
    s->DistanceFactor = 1.0f;
    s->flags |= 0x40;
    s->RolloffFactor = 10.0f;
    s->flags |= 0x10;
    return l;
}

// 0x00298A20
extern "C" RwaObjDef *RwaListenerRegisterUsingInterface(void)
{
    RwaObjDef *def = RwaObjDefCreate(_rwaGnullObjDef, 0xC, &D_01E7D260, &D_01E7D240, &_listenerAliasInfo, 0x3080C);
    if (!def)
        return 0;
    RwaObjDefAssignID(def, &rwaLISTENERID, 0);
    func_00297C30_inl3(def, 0x4C, 0x40);
    def->createFunc = _RwaListenerCreate;
    def->createParamSize = 0;
    def->noFreeListEntries = 1;
    if (!RwaObjDefSetupParams(def, _listenerInputParams, 8, _listenerOutputParams, 9)) {
        RwaObjDefDestroy(def);
        return 0;
    }
    RwaObjInterfaceDef *intf = RwaListenerInterfaceRegister();
    if (!intf) {
        RwaObjDefDestroy(def);
        return 0;
    }
    RwaObjDefSetInterface(def, intf);
    D_004E2B68 = 1;
    return def;
}

// 0x00298D80
extern "C" RwaObj *RwaListenerCreate(RwUInt32 p0, RwUInt32 p1, RwUInt32 p2)
{
    return RwaObjCreate(&D_01E7D260, 0, p0, 0, p1, p2);
}
