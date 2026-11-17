// rwavoice.o (librwacore.a) -- the generic voice interface definition (9 settable, 10 readable parameters).
// Retail 0x294550-0x2945C0 [C E map].
#include "rwa/rwacore.h"

extern RwaObjInterfaceDef D_01E7CFB0;            // the definition (static struct)
extern RwaUUID rwaVOICEINTERFACEID;
extern RwaObjInterfaceDefParam _rwaVoiceIntInputs[9];
extern RwaObjInterfaceDefParam _rwaVoiceIntOutputs[10];
extern "C" RwaObjInterfaceDef *RwaObjInterfaceDefCreate(RwaObjInterfaceDef *def);
extern "C" RwaObjInterfaceDef *RwaObjInterfaceDefAssignID(RwaObjInterfaceDef *def, RwaUUID *uuid, const RwChar *name);
extern "C" RwaObjInterfaceDef *RwaObjInterfaceDefSetup(RwaObjInterfaceDef *def, RwaObjInterfaceDefParam *setters,
                                                       RwUInt32 nSet, RwaObjInterfaceDefParam *getters, RwUInt32 nGet);

// 0x00294550
extern "C" RwaObjInterfaceDef *RwaVoiceInterfaceRegister(void)
{
    if (!RwaObjInterfaceDefCreate(&D_01E7CFB0))
        return 0;
    RwaObjInterfaceDefAssignID(&D_01E7CFB0, &rwaVOICEINTERFACEID, 0);
    RwaObjInterfaceDefSetup(&D_01E7CFB0, _rwaVoiceIntInputs, 9, _rwaVoiceIntOutputs, 10);
    return &D_01E7CFB0;
}
