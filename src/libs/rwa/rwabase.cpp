// rwabase.o (librwacore.a) -- RWA's core: open (code modules, memory interface, the RenderWare engine when the game
// does not run one itself), the per-tick update pair, default object registration. Retail 0x290B10-0x291054 [C E map].
// Not ported: RwaSizeOf 0x290B10 and RwaCoreGetTickTime 0x290CE0.
#include "rwa/rwacore.h"

// A code module: open and close callbacks, 8 bytes [C _RwaCoreOpenCodeModules 0x290EC8 / 0x290F08].
typedef RwBool (*UnkFn_00290E90_open)(void);
typedef void (*UnkFn_00290E90_close)(void);
struct UnkStruct_00290E90 {
    UnkFn_00290E90_open field_0;
    UnkFn_00290E90_close field_4;
};
typedef RwBool (*UnkFn_00290E90_cb)(UnkStruct_00290E90 *module, RwUInt32 index);

extern UnkStruct_00290E90 _codeModules[17];
extern void *_interfaces[3];
extern void *D_004E1C10[1];          // E map name here: _rwaGHeaders_configured_for_RELEASE_LIBARARY_define_RWADEBUG_or_
                                     // RWAMETRICS_for_build; used as the 1-entry object-definition list [C 0x290C90]
extern void *D_004E1C14[1];          // E map name here: _objDefs; used as the 1-entry wave-definition list [C 0x290CAC]
extern RwUInt32 _tickTime;
extern RwUInt32 _updating;
extern void *_rwMemFuncs;
extern RwBool _usingRwG;
extern RwUInt32 _rwaGblockAlign;
extern RwBool _rwaGcoreOpen;
extern "C" RwBool _rwaObjInterfaceRegister(void *funcs, RwUInt32 n);
extern "C" RwBool _rwaObjDefRegister(void *funcs, RwUInt32 n);
extern "C" RwBool _rwaWaveDefRegister(void *funcs, RwUInt32 n);
extern "C" RwaObj *RwaObjGetTopLevelObject(void);
extern "C" RwaObj *_rwaObjFinishExecute(RwaObj *obj, RwBool childrenOnly);
extern "C" RwaObj *_rwaObjStartExecute(RwaObj *obj, RwBool childrenOnly);
extern "C" void _rwaVoiceManagerUpdateTimeStamp(void);
extern "C" void _rwaVoiceManagerUpdate(void);
extern "C" void _RwaWaveRendererSubCreateGlobalNULLPacket(RwUInt32 size);
extern "C" RwBool func_00293340(const void *funcs);
extern "C" void _rwaMemoryClose(void);
extern "C" void *RwOsGetMemoryInterface(void);
extern "C" RwBool RwEngineInit(void *memFuncs, RwUInt32 flags, RwUInt32 resArenaSize);
extern "C" RwBool RwEngineOpen(void *params);
extern "C" RwBool RwEngineStart(void);
extern "C" RwBool RwEngineStop(void);
extern "C" RwBool RwEngineClose(void);
extern "C" RwBool RwEngineTerm(void);
extern "C" void _RwaCoreClosePlatform(void);
extern "C" RwBool _RwaCoreOpenPlatform(void);

// 0x00290C70
extern "C" RwBool RwaCoreRegisterDefaultObjects(void)
{
    if (_rwaObjInterfaceRegister(_interfaces, 3) != 1)
        return 0;
    if (_rwaObjDefRegister(D_004E1C10, 1) != 1)
        return 0;
    if (_rwaWaveDefRegister(D_004E1C14, 1) != 1)
        return 0;
    return 1;
}

// The end of an RWA tick: the top-level object's finish-execute tree, then the tick count.
// 0x00290CF0
extern "C" void RwaCoreUpdateFinish(void)
{
    _rwaObjFinishExecute(RwaObjGetTopLevelObject(), 0);
    _updating = 0;
    _tickTime = _tickTime + 1;
}

// The start of an RWA tick: voice time stamp, the start-execute tree, the voice manager (virtual -> real voices).
// 0x00290D30
extern "C" void RwaCoreUpdateStart(void)
{
    _updating = 1;
    _rwaVoiceManagerUpdateTimeStamp();
    _rwaObjStartExecute(RwaObjGetTopLevelObject(), 0);
    _rwaVoiceManagerUpdate();
}

// 0x00290DE0
extern "C" void _RwaCoreCloseModuleCallback(UnkStruct_00290E90 *module)
{
    if (module->field_4 == _RwaCoreClosePlatform)
        _rwaMemoryClose();
}

// After the platform module opened: the memory interface (the caller's, else RenderWare's).
// 0x00290E20
extern "C" RwBool _RwaCoreOpenModuleCallback(UnkStruct_00290E90 *module)
{
    if (module->field_0 != _RwaCoreOpenPlatform)
        return 1;
    if (!_rwMemFuncs)
        _rwMemFuncs = RwOsGetMemoryInterface();
    return func_00293340(_rwMemFuncs) == 1 ? 1 : 0;
}

// Opens the modules in order, calling `opened(module, i)` after each; on the first failure (of a module or of the
// callback) closes the ones counted as open in reverse with `closed(module, i)`, and returns 0.
// 0x00290E90
extern "C" RwBool _RwaCoreOpenCodeModules(UnkStruct_00290E90 *modules, RwUInt32 n, UnkFn_00290E90_cb opened,
                                          UnkFn_00290E90_cb closed)
{
    RwUInt32 i = 0;
    while (i < n) {
        RwBool r = modules[i].field_0();
        if (r) {
            if (opened)
                r = opened(&modules[i], i);
            i++;
        }
        if (!r) {
            while (i) {
                modules[i - 1].field_4();
                if (closed)
                    closed(&modules[i - 1], i - 1);
                i--;
            }
            return 0;
        }
    }
    return 1;
}

// 0x00290D70
extern "C" RwBool _rwaCoreOpen(void *memFuncs, RwBool usingRwG)
{
    _rwMemFuncs = memFuncs;
    _usingRwG = usingRwG;
    _rwaGblockAlign = 0x10;
    if (_RwaCoreOpenCodeModules(_codeModules, 0x11, (UnkFn_00290E90_cb)_RwaCoreOpenModuleCallback,
                                (UnkFn_00290E90_cb)_RwaCoreCloseModuleCallback) != 1)
        return 0;
    _rwaGcoreOpen = 1;
    _updating = 0;
    _tickTime = 0;
    _RwaWaveRendererSubCreateGlobalNULLPacket(0x2000);
    return 1;
}

// 0x00290F70
extern "C" void _RwaCoreClosePlatform(void)
{
    if (_usingRwG)
        return;
    RwEngineStop();
    RwEngineClose();
    RwEngineTerm();
}

// Without a game-run RenderWare (`_usingRwG` 0), RWA starts its own engine: Init(memory, 0, 0x400), Open({1}), Start.
// 0x00290FC0
extern "C" RwBool _RwaCoreOpenPlatform(void)
{
    if (_usingRwG)
        return 1;
    if (!RwEngineInit(_rwMemFuncs, 0, 0x400))
        return 0;
    RwUInt32 params = 1;
    if (!RwEngineOpen(&params)) {
        RwEngineTerm();
        return 0;
    }
    if (RwEngineStart())
        return 1;
    RwEngineClose();
    RwEngineTerm();
    return 0;
}

// The byte size of an RwaType bit (0 for any other value) [C: a switch on the low 16 bits].
// 0x00290B10
extern "C" RwUInt32 RwaSizeOf(RwUInt32 type)
{
    switch (type & 0xFFFF) {
    case 0x1000: case 0x2000: case 0x800: case 0x400:
        return 4;
    case 0x200: case 0x100:
        return 0x10;
    case 0x80: case 0x40:
        return 8;
    case 0x20: case 0x10:
        return 4;
    case 0x8: case 0x4:
        return 2;
    case 0x2: case 0x1:
        return 1;
    default:
        return 0;
    }
}

// 0x00290CE0
extern "C" RwUInt32 RwaCoreGetTickTime(void)
{
    return _tickTime;
}
