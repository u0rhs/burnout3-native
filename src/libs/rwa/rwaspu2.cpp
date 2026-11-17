// rwaspu2.o (librwaspu2.a) -- registers the SPU2 driver's interfaces, object and wave definitions. Retail
// 0x2B0750-0x2B07F8 [C E map]. C port written from retail's code (audio1); proven by the checker on live captures.
#include "rwa/rwacore.h"

extern RwUInt8 D_0049F5F8[0xC];            // the interface registration table (3 entries) [G size]
extern RwUInt8 _objDefsInterfaces[0x18];   // 6 object definitions [G size]
extern RwUInt8 _waveDefs[4];
extern RwUInt8 _interfacesUnregister[0xC];
extern RwUInt8 _objDefsUnregister[0x18];
extern RwUInt8 _waveDefsUnregister[4];
extern "C" RwBool _rwaObjInterfaceRegister(void *table, RwUInt32 n);
extern "C" RwBool _rwaObjDefRegister(void *table, RwUInt32 n);
extern "C" RwBool _rwaWaveDefRegister(void *table, RwUInt32 n);
extern "C" RwInt32 _rwaObjInterfaceUnregister(void *table, RwUInt32 n);
extern "C" RwInt32 _rwaObjDefUnregister(void *table, RwUInt32 n);
extern "C" RwInt32 _rwaWaveDefUnregister(void *table, RwUInt32 n);

// All or nothing: on a failure what was registered is unregistered again (as far as each step succeeds).
// 0x002B0750
extern "C" RwBool RwaSPU2RegisterUsingInterface(void)
{
    if (_rwaObjInterfaceRegister(D_0049F5F8, 3) && _rwaObjDefRegister(_objDefsInterfaces, 6) &&
        _rwaWaveDefRegister(_waveDefs, 1))
        return 1;
    if (_rwaObjInterfaceUnregister(_interfacesUnregister, 3) == 1 && _rwaObjDefUnregister(_objDefsUnregister, 6) == 1)
        _rwaWaveDefUnregister(_waveDefsUnregister, 1);
    return 0;
}
