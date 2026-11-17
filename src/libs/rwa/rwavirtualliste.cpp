// rwavirtualliste.o (librwacore.a) -- virtual listeners (the game's cameras as listeners; the listener manager picks
// one per voice). Retail 0x2AEA50-0x2AEFAC [C E map]. C port written from retail's code (audio1); every function is
// proven by the checker on live captures.
#include "rwa/rwacore.h"
#include "rwa/rwalistener.h"

extern RwaObjDef D_01E81130;               // the virtual listener object definition (static struct)
extern RwaObjDefInterfaceInfo D_01E81110;  // its interface info (static) [G type]
extern RwaObjInterfaceDef D_01E81190;      // the virtual listener interface definition (static struct)
extern RwLinkList _virtualListenerAliasInfo;
extern RwUInt32 D_004E2C10;                // _objInterfaceRegistered of rwavirtualliste [L name, retail_data_names.csv]
extern RwaObjDef *_rwaGnullObjDef;
extern RwaUUID rwaVIRTUALLISTENERID;
extern RwaUUID rwaVIRTUALLISTENERINTERFACEID;
extern RwaInputParamDef _virtualListenerInputParams[5];
extern RwaOutputParamDef _virtualListenerOutputParams[6];
extern RwaObjInterfaceDefParam _rwaVirtualListenerIntInputs[5];
extern RwaObjInterfaceDefParam _rwaVirtualListenerIntOutputs[6];
extern "C" RwaObjDef *RwaObjDefCreate(RwaObjDef *parent, RwUInt32 p1, RwaObjDef *def, void *intfInfo, void *aliasInfo,
                                      RwUInt32 hint);
extern "C" RwaObjDef *RwaObjDefAssignID(RwaObjDef *def, RwaUUID *id, const char *name);
extern "C" RwaObjDef *RwaObjDefSetupParams(RwaObjDef *def, RwaInputParamDef *inputs, RwUInt32 nInputs,
                                       RwaOutputParamDef *outputs, RwUInt32 nOutputs);
extern "C" RwaObjDef *RwaObjDefSetInterface(RwaObjDef *def, RwaObjInterfaceDef *intf);
extern "C" RwaObjDef *RwaObjDefDestroy(RwaObjDef *def);
extern "C" RwaObj *RwaObjCreate(RwaObjDef *def, RwaObj *p1, RwUInt32 p2, void *p3, RwUInt32 p4, RwUInt32 p5);
extern "C" RwaObjInterfaceDef *RwaObjInterfaceDefCreate(RwaObjInterfaceDef *def);
extern "C" RwBool RwaObjInterfaceDefDestroy(RwaObjInterfaceDef *def);
extern "C" RwaObjInterfaceDef *RwaObjInterfaceDefAssignID(RwaObjInterfaceDef *def, RwaUUID *id, const char *name);
extern "C" void *RwaObjInterfaceDefSetup(RwaObjInterfaceDef *def, RwaObjInterfaceDefParam *inputs, RwUInt32 nInputs,
                                         RwaObjInterfaceDefParam *outputs, RwUInt32 nOutputs);
extern "C" void func_002AEA20(RwaVirtualListener *vl);   // destroy: unlinks it from the manager's list
extern "C" RwaObjInterfaceDef *RwaVirtualListenerInterfaceRegister(void);
extern "C" RwaObjInterfaceDef *RwaVirtualListenerInterfaceUnregister(void);

// Creation: at the origin, at rest, facing +z with +y up.
// 0x002AEA50
extern "C" RwaVirtualListener *_RwaVirtualListenerCreate(RwaVirtualListener *vl)
{
    RwaVirtualListenerState *s = &vl->state;
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
    return vl;
}

// 0x002AEB50
extern "C" RwaObjDef *RwaVirtualListenerUnregister(void)
{
    if (D_004E2C10) {
        if (!RwaVirtualListenerInterfaceUnregister())
            return 0;
        D_004E2C10 = 0;
    }
    return RwaObjDefDestroy(&D_01E81130);
}

// 0x002AEBA0
extern "C" RwaObjDef *RwaVirtualListenerRegisterUsingInterface(void)
{
    RwaObjDef *def = RwaObjDefCreate(_rwaGnullObjDef, 0xC, &D_01E81130, &D_01E81110, &_virtualListenerAliasInfo,
                                     0x3080C);
    if (!def)
        return 0;
    RwaObjDefAssignID(def, &rwaVIRTUALLISTENERID, 0);
    func_00297C30_inl3(def, 0x48, 0x40);
    def->createFunc = _RwaVirtualListenerCreate;
    def->destroyFunc = func_002AEA20;
    def->createParamSize = 0;
    if (!RwaObjDefSetupParams(def, _virtualListenerInputParams, 5, _virtualListenerOutputParams, 6)) {
        RwaObjDefDestroy(def);
        return 0;
    }
    RwaObjInterfaceDef *intf = RwaVirtualListenerInterfaceRegister();
    if (!intf) {
        RwaObjDefDestroy(def);
        return 0;
    }
    RwaObjDefSetInterface(def, intf);
    D_004E2C10 = 1;
    return def;
}

// 0x002AEF00
extern "C" RwaObj *RwaVirtualListenerCreate(RwUInt32 p0, RwUInt32 p1, RwUInt32 p2, RwUInt32 p3)
{
    return RwaObjCreate(&D_01E81130, (RwaObj *)p0, p1, 0, p2, p3);
}

// 0x002AEF30
extern "C" RwaObjInterfaceDef *RwaVirtualListenerInterfaceUnregister(void)
{
    return (RwaObjInterfaceDef *)RwaObjInterfaceDefDestroy(&D_01E81190);
}

// 0x002AEF40
extern "C" RwaObjInterfaceDef *RwaVirtualListenerInterfaceRegister(void)
{
    if (!RwaObjInterfaceDefCreate(&D_01E81190))
        return 0;
    RwaObjInterfaceDefAssignID(&D_01E81190, &rwaVIRTUALLISTENERINTERFACEID, 0);
    RwaObjInterfaceDefSetup(&D_01E81190, _rwaVirtualListenerIntInputs, 5, _rwaVirtualListenerIntOutputs, 6);
    return &D_01E81190;
}
