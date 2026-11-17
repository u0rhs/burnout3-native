// RenderWare 3.6 core: immediate-mode 3D (RW 3.1 file name: src/baim3d.c; reading aid only).
// Written from the retail asm (decomp assembly/asm/rw/renderware.s). Tags: [C] confirmed from the asm, [L], [G].

extern unsigned char ourGlobals[];     // [C] 0x00670BD0, the RW globals block (0x4000 bytes, symbol table)
extern int _rwIm3DModule;              // [C] gp-relative 0x004E28B8 (8 bytes): +0 the Im3D globals' offset in ourGlobals
extern int D_004E28BC;                 // [C] gp-relative 0x004E28BC = _rwIm3DModule + 4: the module's open count
extern unsigned char *_rwIm3DGlobals;  // [C] gp-relative 0x004E1910: ourGlobals + the module offset

extern "C" void *memset(void *dst, int value, unsigned int count);

// The platform pipelines of the sky2 driver are empty in retail (stubs).

// [C] a stub: returns 1 ([L] the platform transform pipeline is not created on PS2).
// 0x00211C10
extern "C" int _rwIm3DCreatePlatformTransformPipeline(void *globals)
{
    return 1;
}

// [C] a stub: returns 1.
// 0x00211C18
extern "C" int _rwIm3DCreatePlatformRenderPipelines(void *globals)
{
    return 1;
}

// [C] a stub: does nothing ([G] destroys the transform pipeline).
// 0x00211C20
extern "C" void func_00211C20(void *globals)
{
}

// [C] a stub: does nothing.
// 0x00211C28
extern "C" void _rwIm3DDestroyPlatformRenderPipelines(void *globals)
{
}

// [C] the open count (_rwIm3DModule+4) += 1 (read first), _rwIm3DModule = offset, the count stored, _rwIm3DGlobals =
// ourGlobals + offset, memset(globals, 0, 0x7C). Then _rwIm3DCreatePlatformTransformPipeline(ourGlobals + offset + 0x1C)
// and _rwIm3DCreatePlatformRenderPipelines(ourGlobals + offset + 0x20) (module offset re-read for each); when both give
// non-zero returns the instance. Else _rwIm3DDestroyPlatformRenderPipelines(+0x20 view), func_00211C20(+0x1C view),
// the count -= 1, returns 0.
// 0x0020EA98
extern "C" void *_rwIm3DOpen(void *instance, int offset, int size)
{
    int count = D_004E28BC;
    unsigned char *globals = ourGlobals + offset;

    count = count + 1;
    _rwIm3DModule = offset;
    D_004E28BC = count;
    _rwIm3DGlobals = globals;
    memset(globals, 0, 0x7C);
    if (_rwIm3DCreatePlatformTransformPipeline(ourGlobals + 0x1C + _rwIm3DModule) != 0) {
        if (_rwIm3DCreatePlatformRenderPipelines(ourGlobals + 0x20 + _rwIm3DModule) != 0) {
            return instance;
        }
    }
    _rwIm3DDestroyPlatformRenderPipelines(ourGlobals + 0x20 + _rwIm3DModule);
    func_00211C20(ourGlobals + 0x1C + _rwIm3DModule);
    D_004E28BC = D_004E28BC - 1;
    return 0;
}

// [C] _rwIm3DDestroyPlatformRenderPipelines(ourGlobals + 0x20 + module), func_00211C20(ourGlobals + 0x1C + module),
// open count -= 1; returns the instance.
// 0x0020EB58
extern "C" void *_rwIm3DClose(void *instance, int offset, int size)
{
    _rwIm3DDestroyPlatformRenderPipelines(ourGlobals + 0x20 + _rwIm3DModule);
    func_00211C20(ourGlobals + 0x1C + _rwIm3DModule);
    D_004E28BC = D_004E28BC - 1;
    return instance;
}
