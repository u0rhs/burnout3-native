// rwaoutput.o (librwacore.a) -- the generic output interface definition (6 settable, 5 readable parameters).
// Retail 0x294D30-0x294DA0 [C E map].
#include "rwa/rwacore.h"

extern RwaObjInterfaceDef D_01E7D010;            // the definition (static struct)
extern RwaUUID rwaOUTPUTINTERFACEID;
extern RwaObjInterfaceDefParam _rwaOutputIntInputs[6];
extern RwaObjInterfaceDefParam _rwaOutputIntOutputs[5];
extern "C" RwaObjInterfaceDef *RwaObjInterfaceDefCreate(RwaObjInterfaceDef *def);
extern "C" RwaObjInterfaceDef *RwaObjInterfaceDefAssignID(RwaObjInterfaceDef *def, RwaUUID *uuid, const RwChar *name);
extern "C" RwaObjInterfaceDef *RwaObjInterfaceDefSetup(RwaObjInterfaceDef *def, RwaObjInterfaceDefParam *setters,
                                                       RwUInt32 nSet, RwaObjInterfaceDefParam *getters, RwUInt32 nGet);

// 0x00294D30
extern "C" RwaObjInterfaceDef *RwaOutputInterfaceRegister(void)
{
    if (!RwaObjInterfaceDefCreate(&D_01E7D010))
        return 0;
    RwaObjInterfaceDefAssignID(&D_01E7D010, &rwaOUTPUTINTERFACEID, 0);
    RwaObjInterfaceDefSetup(&D_01E7D010, _rwaOutputIntInputs, 6, _rwaOutputIntOutputs, 5);
    return &D_01E7D010;
}
