// RenderWare 3.6 core: the engine (RW 3.1 file name: src/badevice.c; reading aid only).
// Written from the retail asm (decomp assembly/asm/rw/renderware.s). Tags: [C] confirmed from the asm, [L], [G].

struct UnkStruct_ourGlobals {
    unsigned char pad_0[0x10];
    float field_10;                     // [C] RwEngineStart passes it to RwImageSetGamma ([L] dOpenDevice.gammaCorrection)
    int (*field_14)(int, void *, void *, int);  // [C] (request, out, inOut, numIn) ([L] dOpenDevice.fpSystem)
    unsigned char pad_18[0x11C];
    void *(*field_134)(unsigned int);   // [C] MallocWrapper: jalr on ourGlobals + 0x134 with a0 = *arg ([L] rwmalloc)
    void (*field_138)(void *);          // [C] FreeWrapper: jalr on ourGlobals + 0x138 with a0 = a1 ([L] rwfree)
    unsigned char pad_13C[0x8];
    void *(*field_144)(void *, unsigned int);  // [C] MallocWrapper or _rwFreeListAllocReal; called (free list, hint)
    void *(*field_148)(void *, void *);        // [C] FreeWrapper or _rwFreeListFreeReal; called (free list, block)
    unsigned char pad_14C[0x4];
    int field_150;                      // [C] set to 1 when RwEngineInit succeeded; tested first ([L] engine initialised)
    unsigned int field_154;             // [C] RwEngineInit's third argument ([L] resource arena size)
};
extern UnkStruct_ourGlobals ourGlobals;
extern int engineInstancesOpened;      // [C] gp-relative
extern unsigned char engineTKList[];   // [C] the engine's plugin registry (0x18 bytes, symbol table)

extern "C" void _rwFreeListEnable(int);
extern "C" void *_rwFreeListAllocReal(void *, unsigned int);
extern "C" void *_rwFreeListFreeReal(void *, void *);
extern "C" int _rwStringOpen(void);
extern "C" void _rwStringClose(void);
extern "C" int _rwMemoryOpen(const void *memFuncs);
extern "C" void _rwMemoryClose(void);
extern "C" int _rwFileSystemOpen(void);
extern "C" void _rwFileSystemClose(void);
extern "C" int _rwPluginRegistryOpen(void);
extern "C" int _rwPluginRegistryClose(void);
extern "C" int CorePluginAttach(void);
extern "C" int _rwDeviceRegisterPlugin(void);
extern "C" void *MallocWrapper(unsigned int *size);
extern "C" void *FreeWrapper(void *object, void *mem);
extern "C" int _rwPluginRegistryAddPlugin(void *registry, int size, unsigned int pluginID, void *constructCB,
                                          void *destructCB, void *copyCB);

// 0x001E34D8
extern "C" int _rwGetNumEngineInstances(void)
{
    return engineInstancesOpened;
}

// 0x001E34E0
extern "C" void *MallocWrapper(unsigned int *size)
{
    return ourGlobals.field_134(*size);
}

// 0x001E3508
extern "C" void *FreeWrapper(void *object, void *mem)
{
    ourGlobals.field_138(mem);
    return object;
}

// [C] a tail call of _rwPluginRegistryAddPlugin(&engineTKList, size, pluginID, constructCB, destructCB, 0): the sixth
// argument is the copy callback (stored at entry + 0x28; 0 = the default func_001DE190 [C batkreg.c])
// 0x001E3150
extern "C" int RwEngineRegisterPlugin(int size, unsigned int pluginID, void *constructCB, void *destructCB)
{
    return _rwPluginRegistryAddPlugin(engineTKList, size, pluginID, constructCB, destructCB, 0);
}

// [C] initFlags bit 0 ([L] rwENGINEINITNOFREELISTS): free lists off, objects straight from the memory functions; the
// subsystems open in order and close in reverse order when one fails; nothing happens when already initialised
// (field_150), which returns 0.
// 0x001E2FF8
extern "C" int RwEngineInit(const void *memFuncs, unsigned int initFlags, unsigned int resArenaSize)
{
    int result = 0;
    if (initFlags & 1) {
        ourGlobals.field_148 = (void *(*)(void *, void *))FreeWrapper;
        ourGlobals.field_144 = (void *(*)(void *, unsigned int))MallocWrapper;
        _rwFreeListEnable(0);
    } else {
        ourGlobals.field_148 = _rwFreeListFreeReal;
        ourGlobals.field_144 = _rwFreeListAllocReal;
        _rwFreeListEnable(1);
    }
    ourGlobals.field_154 = resArenaSize;
    if (ourGlobals.field_150 != 0) {
        return 0;
    }
    result = _rwStringOpen();
    if (result == 0) {
        return result;
    }
    result = _rwMemoryOpen(memFuncs);
    if (result != 0) {
        result = _rwFileSystemOpen();
        if (result != 0) {
            result = _rwPluginRegistryOpen();
            if (result != 0) {
                result = CorePluginAttach();
                if (result != 0) {
                    result = _rwDeviceRegisterPlugin();
                    if (result != 0) {
                        ourGlobals.field_150 = 1;
                        return result;
                    }
                }
                _rwPluginRegistryClose();
            }
            _rwFileSystemClose();
        }
        _rwMemoryClose();
    }
    _rwStringClose();
    return result;
}

// [C] RwError on the stack: {plugin ID 1 ([L] rwID_COREPLUGIN), _rwerror(...)} ([L] RwError).
struct UnkStruct_RwErrorSet {
    int field_0;
    int field_4;
};

// [C] _rwerror (0x001D89B8) only returns its first argument (see baerr.cpp); declared with the code alone, as in
// badevice_more.cpp / batkreg.cpp.
extern "C" int _rwerror(int code);
extern "C" UnkStruct_RwErrorSet *RwErrorSet(UnkStruct_RwErrorSet *code);
extern "C" void *_rwPluginRegistryInitObject(void *registry, void *object);
extern "C" int RwImageSetGamma(float gamma);

// [C] the device system function (ourGlobals + 0x14) with request 7, out 0, inOut 0, numIn = modeIndex ([L] 3.1
// rwDEVICESYSTEMUSEMODE); a 0 answer sets error {1, _rwerror(0x18, 7)}. Returns whether the answer was nonzero.
// 0x001E2A98
extern "C" int RwEngineSetVideoMode(int modeIndex)
{
    int result;
    UnkStruct_RwErrorSet err;

    result = ourGlobals.field_14(7, 0, 0, modeIndex);
    if (result == 0) {
        err.field_0 = 1;
        err.field_4 = _rwerror(0x18);   // [C] retail also passes 7 as a second (ignored) argument
        RwErrorSet(&err);
    }
    return result != 0;
}

// [C] request 2 (all arguments 0; [L] rwDEVICESYSTEMSTART): a 0 answer sets error {1, _rwerror(0x18, 2)} and returns
// 0. Then the engine plugins' constructors run over ourGlobals itself (_rwPluginRegistryInitObject(&engineTKList,
// &ourGlobals)). When they succeed: RwImageSetGamma(ourGlobals + 0x10), request 0x11 (all 0; [L]
// rwDEVICESYSTEMFINALIZESTART; its result ignored), field_150 = 3 ([L] rwENGINESTATUSSTARTED), returns 1. When they fail:
// request 3 (all 0; [L] rwDEVICESYSTEMSTOP); a 0 answer sets error {1, _rwerror(0x18, 3)}; returns 0.
// 0x001E2D10
extern "C" int RwEngineStart(void)
{
    int result;
    UnkStruct_RwErrorSet err;

    result = ourGlobals.field_14(2, 0, 0, 0);
    if (result == 0) {
        err.field_0 = 1;
        err.field_4 = _rwerror(0x18);   // [C] retail also passes 2 as a second (ignored) argument
        RwErrorSet(&err);
        return 0;
    }
    if (_rwPluginRegistryInitObject(engineTKList, &ourGlobals) != 0) {
        RwImageSetGamma(ourGlobals.field_10);
        ourGlobals.field_14(0x11, 0, 0, 0);
        ourGlobals.field_150 = 3;
        return 1;
    }
    if (ourGlobals.field_14(3, 0, 0, 0) != 0) {
        return 0;
    }
    err.field_0 = 1;
    err.field_4 = _rwerror(0x18);       // [C] retail also passes 3 as a second (ignored) argument
    RwErrorSet(&err);
    return 0;
}

// [C] request 1 (all arguments 0; [L] rwDEVICESYSTEMCLOSE): a 0 answer sets error {1, _rwerror(0x18, 1)}; a nonzero
// one sets field_150 = 1 ([L] back to initialised) and then decrements engineInstancesOpened. Returns the answer as is.
// 0x001E2E20
extern "C" int RwEngineClose(void)
{
    int result;
    UnkStruct_RwErrorSet err;

    result = ourGlobals.field_14(1, 0, 0, 0);
    if (result == 0) {
        err.field_0 = 1;
        err.field_4 = _rwerror(0x18);   // [C] retail also passes 1 as a second (ignored) argument
        RwErrorSet(&err);
    }
    if (result != 0) {
        ourGlobals.field_150 = 1;
        engineInstancesOpened = engineInstancesOpened - 1;
    }
    return result;
}

// [C] only with no engine instance open (engineInstancesOpened == 0, else 0 is returned and nothing happens): the
// plugin registries, the file system and memory close in that order, then field_150 = 0. Returns 1.
// 0x001E31B8
extern "C" int RwEngineTerm(void)
{
    int result = engineInstancesOpened == 0;

    if (result != 0) {
        _rwPluginRegistryClose();
        _rwFileSystemClose();
        _rwMemoryClose();
        ourGlobals.field_150 = 0;
    }
    return result;
}
