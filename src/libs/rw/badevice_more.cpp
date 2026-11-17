// RenderWare 3.6 core: the engine, more (RW 3.1 file name: src/badevice.c; reading aid only). A second file for the
// functions drafted apart from badevice.cpp (boot1 staging).
// Written from the retail asm (decomp assembly/asm/rw/renderware.s). Tags: [C] confirmed from the asm, [L], [G].
// boot1 staging (rw_wip).

// [C] the device _rwDeviceGetHandle returns ([L] RwDevice): RwEngineOpen and _rwDeviceSystemRequest use only +4.
struct UnkStruct_rwDeviceGetHandle {
    unsigned char pad_0[0x4];                       // [L] gammaCorrection
    int (*field_4)(int, void *, void *, int);       // [C] (request, out, inOut, numIn) ([L] fpSystem)
};

struct UnkStruct_ourGlobals {
    unsigned char pad_0[0x10];
    unsigned char field_10[0x38];                   // [C] out of request 4 in RwEngineOpen ([L] dOpenDevice, RwDevice)
    unsigned char field_48[0x74];                   // [C] out of request 0xB, 0x1D entries ([L] stdFunc)
    unsigned char pad_BC[0x3C];
    char *(*field_F8)(char *, const char *);        // [C] set to strcpy by _rwStringOpen ([L] stringFuncs.vecStrcpy)
    unsigned char pad_FC[0x38];
    unsigned char field_134[0x10];                  // [C] inOut of request 4 ([L] memoryFuncs)
    unsigned char pad_144[0xC];
    int field_150;                                  // [C] 1 after RwEngineInit, 2 after RwEngineOpen ([L] engineStatus)
};
extern UnkStruct_ourGlobals ourGlobals;
extern int engineInstancesOpened;   // [C] gp-relative
extern int skyRasterExt;            // [C] gp-relative
extern int skyCameraExt;            // [C] gp-relative
extern int skyTextureExt;           // [C] gp-relative
extern char D_004B8348[];           // [C] "Only rendering sub system"

// [C] RwError on the stack: {plugin ID 1 ([L] rwID_COREPLUGIN), _rwerror(...)} ([L] RwError).
struct UnkStruct_RwErrorSet {
    int field_0;
    int field_4;
};

// [C] _rwerror (0x001D89B8) is variadic in RW but only returns its first argument (the rest are spilled by its
// va_start prologue and never read), so it is declared with the code alone: b3host's variadic guest_fn does not
// compile at -O2 here (call_guest_varargs is declared after guest_fn<R(A..., ...)> in b3host/call.h).
extern "C" int _rwerror(int code);
extern "C" void *RwErrorSet(UnkStruct_RwErrorSet *code);
extern "C" UnkStruct_rwDeviceGetHandle *_rwDeviceGetHandle(void);   // [C] sky2 driver, not ported here
extern "C" int _rwPipeAttach(void);
// [C] the engine plugins' open / close callbacks ([L] RwPluginObjectConstructor: (object, offset, size)).
extern "C" int RwEngineRegisterPlugin(int size, unsigned int pluginID, void *(*constructCB)(void *, int, int),
                                      void *(*destructCB)(void *, int, int));
extern "C" void *func_001D89F8(void *, int, int);
extern "C" void *func_001D8A28(void *, int, int);
extern "C" void *func_001DE4E0(void *, int, int);
extern "C" void *func_001DE538(void *, int, int);
extern "C" void *func_001D8988(void *, int, int);
extern "C" void *func_001D89A0(void *, int, int);
extern "C" void *_rwMatrixOpen(void *, int, int);
extern "C" void *_rwMatrixClose(void *, int, int);
extern "C" void *_rwFrameOpen(void *, int, int);
extern "C" void *_rwFrameClose(void *, int, int);
extern "C" void *_rwStreamModuleOpen(void *, int, int);
extern "C" void *_rwStreamModuleClose(void *, int, int);
extern "C" void *_rwCameraOpen(void *, int, int);
extern "C" void *_rwCameraClose(void *, int, int);
extern "C" void *_rwImageOpen(void *, int, int);
extern "C" void *_rwImageClose(void *, int, int);
extern "C" void *_rwRasterOpen(void *, int, int);
extern "C" void *_rwRasterClose(void *, int, int);
extern "C" void *_rwTextureOpen(void *, int, int);
extern "C" void *_rwTextureClose(void *, int, int);
extern "C" void *_rwRenderPipelineOpen(void *, int, int);
extern "C" void *_rwRenderPipelineClose(void *, int, int);
extern "C" void *_rwChunkGroupOpen(void *, int, int);
extern "C" void *_rwChunkGroupClose(void *, int, int);
extern "C" void *_rwIm3DOpen(void *, int, int);
extern "C" void *_rwIm3DClose(void *, int, int);
extern "C" void *_rwResourcesOpen(void *, int, int);
extern "C" void *_rwResourcesClose(void *, int, int);
extern "C" int RwRasterRegisterPlugin(int size, unsigned int pluginID, void *constructCB, void *destructCB, void *copyCB);
extern "C" int RwCameraRegisterPlugin(int size, unsigned int pluginID, void *constructCB, void *destructCB, void *copyCB);
extern "C" int RwTextureRegisterPlugin(int size, unsigned int pluginID, void *constructCB, void *destructCB, void *copyCB);
// [C] stream callbacks ([L] 3.1 shapes: read (stream, length, object, offset, size), write (stream, length, object,
// offset, size), getSize (object, offset, size)).
extern "C" void *skyReadTexCB(void *, int, void *, int, int);
extern "C" void *skyWriteTexCB(void *, int, const void *, int, int);
extern "C" int func_001FF9E0(int *);
extern "C" int RwTextureRegisterPluginStream(unsigned int pluginID, void *(*readCB)(void *, int, void *, int, int),
                                             void *(*writeCB)(void *, int, const void *, int, int),
                                             int (*getSizeCB)(int *));   // [C] the callback is func_001FF9E0 (int *): its definition basky_fb.cpp

// [C] the core engine plugins in this order (size, ID): 8 0x40F, 0x18 0x401, 0 0x40D, 0x18 0x402 (matrix), 4 0x403
// (frame), 4 0x404 (stream), 4 0x405 (camera), 0x220 0x406 (image), 0x64 0x407 (raster), 0x34 0x408 (texture), 0x60
// 0x409 (render pipeline), 4 0x412 (chunk group), then _rwPipeAttach, then 0x7C 0x40A (Im3D), 0x28 0x40B (resources).
// All results are ORed; returns 1 unless that is negative.
// 0x001E2738
extern "C" int CorePluginAttach(void)
{
    int status;

    status = RwEngineRegisterPlugin(8, 0x40F, func_001D89F8, func_001D8A28);
    status |= RwEngineRegisterPlugin(0x18, 0x401, func_001DE4E0, func_001DE538);
    status |= RwEngineRegisterPlugin(0, 0x40D, func_001D8988, func_001D89A0);
    status |= RwEngineRegisterPlugin(0x18, 0x402, _rwMatrixOpen, _rwMatrixClose);
    status |= RwEngineRegisterPlugin(4, 0x403, _rwFrameOpen, _rwFrameClose);
    status |= RwEngineRegisterPlugin(4, 0x404, _rwStreamModuleOpen, _rwStreamModuleClose);
    status |= RwEngineRegisterPlugin(4, 0x405, _rwCameraOpen, _rwCameraClose);
    status |= RwEngineRegisterPlugin(0x220, 0x406, _rwImageOpen, _rwImageClose);
    status |= RwEngineRegisterPlugin(0x64, 0x407, _rwRasterOpen, _rwRasterClose);
    status |= RwEngineRegisterPlugin(0x34, 0x408, _rwTextureOpen, _rwTextureClose);
    status |= RwEngineRegisterPlugin(0x60, 0x409, _rwRenderPipelineOpen, _rwRenderPipelineClose);
    status |= RwEngineRegisterPlugin(4, 0x412, _rwChunkGroupOpen, _rwChunkGroupClose);
    status |= _rwPipeAttach();
    status |= RwEngineRegisterPlugin(0x7C, 0x40A, _rwIm3DOpen, _rwIm3DClose);
    status |= RwEngineRegisterPlugin(0x28, 0x40B, _rwResourcesOpen, _rwResourcesClose);
    return -1 < status;
}

// [C] at 0x001FF858, inside the sky2 driver's range (it writes the driver's skyRasterExt / skyCameraExt /
// skyTextureExt), but listed here as the task's badevice addition. Raster plugin 0x5C bytes ID 0x40C, camera plugin
// 0xD8 bytes ID 0x40C, texture plugin 0 bytes ID 0x110 (no callbacks), then the texture stream callbacks for 0x110,
// whose result overwrites skyTextureExt. Stops at the first negative offset; returns whether the last one was >= 0.
// 0x001FF858
extern "C" int _rwDeviceRegisterPlugin(void)
{
    skyRasterExt = RwRasterRegisterPlugin(0x5C, 0x40C, 0, 0, 0);
    if (skyRasterExt < 0) {
        return 0;
    }
    skyCameraExt = RwCameraRegisterPlugin(0xD8, 0x40C, 0, 0, 0);
    if (skyCameraExt < 0) {
        return 0;
    }
    skyTextureExt = RwTextureRegisterPlugin(0, 0x110, 0, 0, 0);
    if (skyTextureExt < 0) {
        return 0;
    }
    skyTextureExt = RwTextureRegisterPluginStream(0x110, skyReadTexCB, skyWriteTexCB, func_001FF9E0);
    return skyTextureExt >= 0;
}

// [C] the device's own handler first (device + 4, called with (request, out, inOut, numIn)); its nonzero result is
// returned. Otherwise the core answers requests 0xD..0x12 (jump table at 0x004B8370, labelled _rwBuildDateTime in the
// tables): 0xD *(int *)out = 1; 0xE when numIn == 0, strcpy (through ourGlobals + 0xF8) of "Only rendering sub system"
// into out; 0xF *(int *)out = 0; 0x10 numIn == 0; 0x11, 0x12 1 ([L] 3.1 GETNUMSUBSYSTEMS / GETSUBSYSTEMINFO /
// GETCURRENTSUBSYSTEM / SETSUBSYSTEM / ...). Anything else, or a 0 answer, sets error {1, _rwerror(0x18, request)}
// and returns 0.
// 0x001E33D0
extern "C" int _rwDeviceSystemRequest(UnkStruct_rwDeviceGetHandle *device, int request, void *out, void *inOut,
                                      int numIn)
{
    int result;
    UnkStruct_RwErrorSet err;

    result = device->field_4(request, out, inOut, numIn);
    if (result != 0) {
        return result;
    }
    switch (request) {
    case 0xD:
        *(int *)out = 1;
        result = 1;
        break;
    case 0xE:
        result = numIn == 0;
        if (result != 0) {
            ourGlobals.field_F8((char *)out, D_004B8348);
        }
        break;
    case 0xF:
        *(int *)out = 0;
        result = 1;
        break;
    case 0x10:
        result = numIn == 0;
        break;
    case 0x11:
    case 0x12:
        result = 1;
        break;
    }
    if (result == 0) {
        err.field_0 = 1;
        err.field_4 = _rwerror(0x18);   // [C] retail also passes request as a second (ignored) argument
        RwErrorSet(&err);
    }
    return result;
}

// [C] needs field_150 == 1 (else error {1, _rwerror(0x80000001)}, 0) and a non-NULL initInfo (else error {1,
// _rwerror(0x80000016)}, 0) and a device. Request 4 (out ourGlobals + 0x10, inOut ourGlobals + 0x134; its result is
// ignored), request 0 (inOut initInfo); when that succeeds request 0xB (out ourGlobals + 0x48, numIn 0x1D),
// engineInstancesOpened + 1, field_150 = 2, and 1 is returned ([L] 3.1: REGISTER, OPEN, STANDARDS).
// 0x001E2EA8
extern "C" int RwEngineOpen(void *initInfo)
{
    int result;
    UnkStruct_rwDeviceGetHandle *device;
    UnkStruct_RwErrorSet err;

    result = ourGlobals.field_150 == 1;
    if (result == 0) {
        err.field_0 = 1;
        err.field_4 = _rwerror(0x80000001);
        RwErrorSet(&err);
        return result;
    }
    result = initInfo != 0;
    if (result == 0) {
        err.field_0 = 1;
        err.field_4 = _rwerror(0x80000016);
        RwErrorSet(&err);
        return result;
    }
    device = _rwDeviceGetHandle();
    result = device != 0;
    if (result == 0) {
        return result;
    }
    _rwDeviceSystemRequest(device, 4, ourGlobals.field_10, ourGlobals.field_134, 0);
    if (_rwDeviceSystemRequest(device, 0, 0, initInfo, 0) != 0) {
        result = 1;
        _rwDeviceSystemRequest(device, 0xB, ourGlobals.field_48, 0, 0x1D);
        engineInstancesOpened = engineInstancesOpened + 1;
    } else {
        result = 0;
    }
    if (result != 0) {
        ourGlobals.field_150 = 2;
    }
    return result;
}
