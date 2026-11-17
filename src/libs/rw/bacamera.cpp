// RenderWare 3.6 core (RW 3.1 file name: src/bacamera.c; reading aid only).
// Written from the retail asm (decomp assembly/asm/rw/renderware.s). Tags: [C] confirmed from the asm, [L], [G].
// boot1 staging (rw_wip).

extern unsigned char cameraTKList[];   // [C] the plugin registry (0x18 bytes, symbol table) ([L] RwPluginRegistry)

extern "C" int _rwPluginRegistryAddPlugin(void *registry, int size, unsigned int pluginID, void *constructCB,
                                          void *destructCB, void *copyCB);

// [C] a call of _rwPluginRegistryAddPlugin(&cameraTKList, size, pluginID, constructCB, destructCB, copyCB), all five
// arguments passed on ([L] copyCB: the sixth argument lands in the entry's +0x28).
// 0x001E2428
extern "C" int RwCameraRegisterPlugin(int size, unsigned int pluginID, void *constructCB, void *destructCB, void *copyCB)
{
    return _rwPluginRegistryAddPlugin(cameraTKList, size, pluginID, constructCB, destructCB, copyCB);
}

extern unsigned char ourGlobals[];     // [C] 0x00670BD0, the RW globals block (0x4000 bytes, symbol table)
extern int cameraModule;               // [C] gp-relative 0x004E2798 (8 bytes): +0 the camera globals' offset in ourGlobals
extern int D_004E279C;                 // [C] gp-relative 0x004E279C = cameraModule + 4: the module's open count
extern int _rwCameraFreeListBlockSize;           // [C] gp-relative
extern int _rwCameraFreeListPreallocBlocks;      // [C] gp-relative
extern unsigned char D_00670BA8[];     // [C] in-place space for the camera free list

extern "C" void *RwFreeListCreateAndPreallocateSpace(int entrySize, int entriesPerBlock, int alignment,
                                                     int numBlocksToPreallocate, void *inPlaceSpace, unsigned int hint);
extern "C" int RwFreeListDestroy(void *freeList);

// [C] the camera globals: only the free list.
struct UnkStruct_cameraModule {
    void *field_0;                     // [C] the camera free list
};

// [C] cameraModule = offset; +0 = RwFreeListCreateAndPreallocateSpace(first word of cameraTKList (the registry's
// current size = the camera size), _rwCameraFreeListBlockSize, 0x10, _rwCameraFreeListPreallocBlocks, D_00670BA8,
// 0x40005); 0 -> returns 0; else open count += 1, returns the instance.
// 0x001E2590
extern "C" void *_rwCameraOpen(void *instance, int offset, int size)
{
    void *freeList;

    cameraModule = offset;
    freeList = RwFreeListCreateAndPreallocateSpace(*(int *)cameraTKList, _rwCameraFreeListBlockSize, 0x10,
                                                   _rwCameraFreeListPreallocBlocks, D_00670BA8, 0x40005);
    ((UnkStruct_cameraModule *)(ourGlobals + cameraModule))->field_0 = freeList;
    if (freeList == 0) {
        return 0;
    }
    D_004E279C = D_004E279C + 1;
    return instance;
}

// [C] if the free list is set: RwFreeListDestroy it and clear it (cameraModule re-read); open count -= 1; returns the
// instance.
// 0x001E2528
extern "C" void *_rwCameraClose(void *instance, int offset, int size)
{
    void *freeList = ((UnkStruct_cameraModule *)(ourGlobals + cameraModule))->field_0;

    if (freeList != 0) {
        RwFreeListDestroy(freeList);
        ((UnkStruct_cameraModule *)(ourGlobals + cameraModule))->field_0 = 0;
    }
    D_004E279C = D_004E279C - 1;
    return instance;
}

// ---------------------------------------------------------------------------------------------------------------
// boot2 additions (track rw): camera create / update / view window / clip planes.

// [C] a camera as these functions touch it ([L] RwCamera; 3.1 order, retail 3.6 offsets from the asm).
struct UnkStruct_RwCameraView {
    unsigned char field_0;             // [C] 4 at create ([L] type rwCAMERA)
    unsigned char field_1;             // [C] 0
    unsigned char field_2;             // [C] 0
    unsigned char field_3;             // [C] 0
    void *field_4;                     // [C] the frame the camera is attached to (0 = none) ([L] parent)
    unsigned char pad_8[0x8];
    int (*field_10)(void *);           // [C] CameraSync ([L] camera sync function)
    int field_14;                      // [C] projection, 1 at create ([L] rwPERSPECTIVE)
    int (*field_18)(void *);           // [C] CameraBeginUpdate at create ([L] beginUpdate)
    int (*field_1C)(void *);           // [C] CameraEndUpdate at create ([L] endUpdate)
    unsigned char pad_20[0xC];
    int field_2C;                      // [C] 0
    unsigned char pad_30[0x30];
    void *field_60;                    // [C] the frame buffer raster, 0 at create ([L] frameBuffer)
    void *field_64;                    // [C] the Z buffer raster, 0 at create ([L] zBuffer)
    float field_68;                    // [C] view window x ([L] viewWindow.x)
    float field_6C;                    // [C] view window y
    float field_70;                    // [C] 1 / view window x ([L] recipViewWindow.x)
    float field_74;                    // [C] 1 / view window y
    float field_78;                    // [C] view offset x ([L] viewOffset.x)
    float field_7C;                    // [C] view offset y
    float field_80;                    // [C] near clip plane ([L] nearPlane)
    float field_84;                    // [C] far clip plane ([L] farPlane)
    float field_88;                    // [C] 5.0 at create ([L] fogPlane)
    float field_8C;                    // [C] ([L] zScale)
    float field_90;                    // [C] ([L] zShift)
};

// [C] the engine globals' two floats the z scale / shift are computed from (ourGlobals + 0x18 / + 0x1C: the near and
// far values of the z buffer range [L]).
struct UnkStruct_ourGlobalsCam {
    unsigned char pad_0[0x18];
    float field_18;
    float field_1C;
    unsigned char pad_20[0x7C];
    int (*field_9C)(void *);           // [C] the camera standard function called by func_001E1FF0 (a0 = the camera)
    unsigned char pad_A0[0xA4];
    void *(*field_144)(void *, unsigned int);   // [C] (free list, hint) ([L] memoryAlloc)
};

extern float D_004E0688;               // [C] gp-relative 0x004E0688: the default near plane
extern float D_004E0690;               // [C] gp-relative 0x004E0690: a z range margin factor ([L] 0.0 .. 1.0)
extern float D_004E06F4;               // [C] gp-relative 0x004E06F4
extern float D_004E06F8;               // [C] gp-relative 0x004E06F8

extern "C" int CameraSync(void *camera);
extern "C" int CameraBeginUpdate(void *camera);
extern "C" int CameraEndUpdate(void *camera);
extern "C" void *_rwPluginRegistryInitObject(void *registry, void *object);
extern "C" void *RwFrameUpdateObjects(void *frame);
extern "C" void *RwRasterShowRaster(void *raster, unsigned int dev, unsigned int flags);

// [C] a camera from the camera free list (ourGlobals + 0x144, hint 0x30005); NULL when that fails. Then bytes 1, 0, 2,
// 3 = 0 / 4 / 0 / 0, +4 = 0, +0x10 = CameraSync, +0x18 / +0x1C = CameraBeginUpdate / CameraEndUpdate, +0x14 = 1,
// view window 1.0 / 1.0, reciprocals 1.0, offset 0, near D_004E0688, far 10.0, +0x88 = 5.0, +0x60 / +0x64 = 0; then the
// z scale / shift (+0x8C / +0x90) from the engine's range (ourGlobals + 0x18 / + 0x1C) as the clip plane setters do,
// +0x2C = 0, and the camera plugins' constructors (_rwPluginRegistryInitObject(cameraTKList, camera)).
// 0x001E1D00
extern "C" UnkStruct_RwCameraView *RwCameraCreate(void)
{
    UnkStruct_RwCameraView *camera;
    UnkStruct_ourGlobalsCam *g = (UnkStruct_ourGlobalsCam *)ourGlobals;
    float f0, f1, f2, f3, f4, f5, f6;

    camera = (UnkStruct_RwCameraView *)g->field_144(*(void **)(ourGlobals + cameraModule), 0x30005);
    if (camera == 0) {
        return 0;
    }
    camera->field_1 = 0;
    camera->field_0 = 4;
    camera->field_2 = 0;
    camera->field_3 = 0;
    camera->field_4 = 0;
    camera->field_10 = CameraSync;
    f0 = 1.0f;
    f4 = 0.0f;
    f3 = D_004E0688;
    f2 = 10.0f;
    f1 = 5.0f;
    camera->field_78 = f4;
    camera->field_80 = f3;
    camera->field_84 = f2;
    camera->field_7C = f4;
    camera->field_70 = f0;
    camera->field_88 = f1;
    camera->field_6C = f0;
    camera->field_68 = f0;
    camera->field_74 = f0;
    f2 = D_004E0690;
    f0 = g->field_1C;
    f1 = g->field_18;
    f3 = D_004E06F4;
    f6 = f0 - f1;
    f4 = D_004E06F8;
    f5 = 0.5f;
    camera->field_18 = CameraBeginUpdate;
    f2 = f6 * f2;
    camera->field_1C = CameraEndUpdate;
    camera->field_14 = 1;
    camera->field_60 = 0;
    f1 = f1 + f2;
    camera->field_64 = 0;
    f0 = f0 - f2;
    f6 = f0 - f1;
    f0 = f0 + f1;
    f3 = f6 / f3;
    f4 = f3 * f4;
    camera->field_8C = f3;
    f0 = f0 - f4;
    f0 = f0 * f5;
    camera->field_90 = f0;
    camera->field_2C = 0;
    _rwPluginRegistryInitObject(cameraTKList, camera);
    return camera;
}

extern "C" void *_rwPluginRegistryDeInitObject(void *registry, void *object);
extern "C" void _rwObjectHasFrameReleaseFrame(void *object);

// [C] the RW allocator's free at ourGlobals + 0x148 ((free list, entry) [L] memoryFree).
struct UnkStruct_ourGlobalsCamFree {
    unsigned char pad_0[0x148];
    void (*field_148)(void *, void *);
};

// fe1 (2026-11-10: the game's shutdown before the EXTRAS page's demo, CB3GraphicsManagerBase::Destruct ->
// CGtViewport::Destruct). [C] the camera plugins' destructors (_rwPluginRegistryDeInitObject(cameraTKList, camera)),
// the camera taken off its frame's object list (_rwObjectHasFrameReleaseFrame), then the camera back to the camera
// free list (ourGlobals + 0x148 with the list at ourGlobals + cameraModule); returns 1.
// 0x001E2068
extern "C" int RwCameraDestroy(UnkStruct_RwCameraView *camera)
{
    _rwPluginRegistryDeInitObject(cameraTKList, camera);
    _rwObjectHasFrameReleaseFrame(camera);
    ((UnkStruct_ourGlobalsCamFree *)ourGlobals)->field_148(*(void **)(ourGlobals + cameraModule), camera);
    return 1;
}

// [C] calls the camera's beginUpdate (+0x18) with the camera; its result is returned.
// 0x001E1FB0
extern "C" int RwCameraBeginUpdate(UnkStruct_RwCameraView *camera)
{
    return camera->field_18(camera);
}

// [C] calls the camera's endUpdate (+0x1C) with the camera; its result is returned.
// 0x001E1FD0
extern "C" int RwCameraEndUpdate(UnkStruct_RwCameraView *camera)
{
    return camera->field_1C(camera);
}

// [C] calls the function at ourGlobals + 0x9C with the camera ([G] a clear / sync request; a1, a2 are not set up by
// the asm); the camera when that answered nonzero, else NULL (movz).
// 0x001E1FF0
extern "C" UnkStruct_RwCameraView *func_001E1FF0(UnkStruct_RwCameraView *camera)
{
    UnkStruct_ourGlobalsCam *g = (UnkStruct_ourGlobalsCam *)ourGlobals;
    int r = g->field_9C(camera);

    if (r == 0) {
        return 0;
    }
    return camera;
}

// [C] RwRasterShowRaster(camera->field_60 (frame buffer), a1, a2) is returned as the camera when nonzero, else NULL
// ([L] 3.1 RwCameraShowRaster(camera, pDev, flags): a1 / a2 are passed through unchanged by the asm).
// 0x001E2028
extern "C" UnkStruct_RwCameraView *RwCameraShowRaster(UnkStruct_RwCameraView *camera, unsigned int dev, unsigned int flags)
{
    void *r = RwRasterShowRaster(camera->field_60, dev, flags);

    if (r == 0) {
        return 0;
    }
    return camera;
}

// [C] the 8 bytes at view are copied to +0x68 (x, y); the reciprocals 1.0 / x, 1.0 / y go to +0x70 / +0x74; if the
// camera has a frame (+4) RwFrameUpdateObjects(frame). Returns the camera.
// 0x001E2110
extern "C" UnkStruct_RwCameraView *RwCameraSetViewWindow(UnkStruct_RwCameraView *camera, const float *view)
{
    float f0, f1, f2;
    float vx = view[0];
    float vy = view[1];

    f0 = 1.0f;
    camera->field_68 = vx;
    camera->field_6C = vy;
    f1 = camera->field_68;
    f2 = camera->field_6C;
    f1 = f0 / f1;
    f0 = f0 / f2;
    camera->field_70 = f1;
    camera->field_74 = f0;
    if (camera->field_4 != 0) {
        RwFrameUpdateObjects(camera->field_4);
    }
    return camera;
}

// [C] +0x80 = near; the z scale / shift (+0x8C / +0x90) are re-derived: with projection 1 (or anything but 2) the
// far value used is 1.0 / far and the near value 1.0 / near (the arg is replaced), with projection 2 (parallel) far
// is used as it is and the arg as it is. With r0 = ourGlobals + 0x18, r1 = + 0x1C and the margin m = D_004E0690:
// d = (r1 - r0) * m; lo = r0 + d; hi = r1 - d; scale = (hi - lo) / (far' - near'); shift = ((hi + lo) - scale *
// (far' + near')) * 0.5. If the camera has a frame (+4), RwFrameUpdateObjects(frame). Returns the camera.
// 0x001E2288
extern "C" UnkStruct_RwCameraView *RwCameraSetNearClipPlane(UnkStruct_RwCameraView *camera, float nearPlane)
{
    float f0, f1, f2, f3, f4, f5, f6;
    UnkStruct_ourGlobalsCam *g = (UnkStruct_ourGlobalsCam *)ourGlobals;
    float f12 = nearPlane;
    float diff;

    camera->field_80 = f12;
    f6 = g->field_1C;
    f5 = g->field_18;
    if (camera->field_14 != 2) {
        f0 = 1.0f;
        f1 = camera->field_84;
        f2 = camera->field_80;
        f4 = f0 / f1;
        f12 = f0 / f2;
    } else {
        f4 = camera->field_84;
    }
    f1 = f6 - f5;
    f0 = D_004E0690;
    f3 = 0.5f;
    f2 = f4 - f12;
    f0 = f1 * f0;
    diff = f2;
    f2 = f4 + f12;
    f5 = f5 + f0;
    f4 = diff;
    f6 = f6 - f0;
    f1 = f6 - f5;
    f0 = f6 + f5;
    f1 = f1 / f4;
    f2 = f1 * f2;
    camera->field_8C = f1;
    f0 = f0 - f2;
    f0 = f0 * f3;
    camera->field_90 = f0;
    if (camera->field_4 != 0) {
        RwFrameUpdateObjects(camera->field_4);
    }
    return camera;
}

// [C] as RwCameraSetNearClipPlane for the far plane: +0x84 = far; with projection 1 (or anything but 2) the far' =
// 1.0 / far and the near' = 1.0 / near, with projection 2 near' = near and far' = the arg; same z scale / shift
// formula (the margin factor is applied in the same order). If the camera has a frame, RwFrameUpdateObjects(frame).
// Returns the camera.
// 0x001E2358
extern "C" UnkStruct_RwCameraView *RwCameraSetFarClipPlane(UnkStruct_RwCameraView *camera, float farPlane)
{
    float f0, f1, f2, f3, f4, f5;
    UnkStruct_ourGlobalsCam *g = (UnkStruct_ourGlobalsCam *)ourGlobals;
    float f12 = farPlane;
    float diff;

    camera->field_84 = f12;
    f5 = g->field_1C;
    f4 = g->field_18;
    if (camera->field_14 != 2) {
        f0 = 1.0f;
        f1 = camera->field_84;
        f2 = camera->field_80;
        f12 = f0 / f1;
        f2 = f0 / f2;
    } else {
        f2 = camera->field_80;
    }
    f1 = f5 - f4;
    f0 = D_004E0690;
    f3 = f12 - f2;
    f2 = f12 + f2;
    f0 = f1 * f0;
    diff = f3;
    f3 = 0.5f;
    f4 = f4 + f0;
    f5 = f5 - f0;
    f1 = f5 - f4;
    f0 = f5 + f4;
    f4 = diff;
    f1 = f1 / f4;
    f2 = f1 * f2;
    camera->field_8C = f1;
    f0 = f0 - f2;
    f0 = f0 * f3;
    camera->field_90 = f0;
    if (camera->field_4 != 0) {
        RwFrameUpdateObjects(camera->field_4);
    }
    return camera;
}

// ---------------------------------------------------------------------------------------------------------------
// boot2 additions (track rw), the camera's per-frame core: view matrix, clip planes, sync, begin / end update.

extern "C" float sqrtf(float x);
extern "C" void *RwMatrixOptimize(void *matrix, const void *tolerance);
extern "C" void *RwBBoxCalculate(void *bbox, const void *verts, int numVerts);
extern "C" void *RwV3dTransformPoints(void *out, const void *in, int count, const void *matrix);
extern "C" unsigned int _rwFrameSyncDirty(void);
extern "C" void _rwPipeInitForCamera(void *camera);

// [C] three floats ([L] RwV3d).
struct UnkStruct_RwV3dView {
    float x;
    float y;
    float z;
};

// [C] one view frustum plane, 0x14 bytes: the normal, the distance (normal . a point on the plane) and one byte per
// normal component, 1 when the component is positive (sign bit clear) and 0 when negative ([L] RwPlane + the
// inf / sup selector bytes of the bounding box tests).
struct UnkStruct_RwPlaneView {
    float nx;                          // [C] +0
    float ny;                          // [C] +4
    float nz;                          // [C] +8
    float d;                           // [C] +0xC
    unsigned char sx;                  // [C] +0x10: (bits(nx) >> 31) + 1
    unsigned char sy;                  // [C] +0x11
    unsigned char sz;                  // [C] +0x12
    unsigned char pad_13;
};

// [C] a camera as the per-frame functions touch it ([L] RwCamera; retail 3.6 offsets from the asm).
struct UnkStruct_RwCameraSync {
    unsigned char pad_0[0x4];
    unsigned char *field_4;            // [C] the camera's frame (its LTM is at +0x50) ([L] parent)
    unsigned char pad_8[0x8];
    int (*field_10)(void *);           // [C] CameraSync
    int field_14;                      // [C] projection: 1 = perspective, else parallel
    unsigned char pad_18[0x8];
    float field_20[16];                // [C] the view matrix (rows of 4; word 3 of row 0 = flags) ([L] viewMatrix)
    unsigned char pad_60[0x8];
    float field_68;                    // [C] view window x
    float field_6C;                    // [C] view window y
    float field_70;                    // [C] 1 / view window x
    float field_74;                    // [C] 1 / view window y
    float field_78;                    // [C] view offset x
    float field_7C;                    // [C] view offset y
    float field_80;                    // [C] near clip plane
    float field_84;                    // [C] far clip plane
    unsigned char pad_88[0xC];
    UnkStruct_RwPlaneView field_94[6]; // [C] the six frustum planes, built by the CameraBuild*ClipPlanes ([L] frustumPlanes)
    float field_10C[6];                // [C] the frustum's bounding box (sup xyz, inf xyz) ([L] frustumBoundBox)
    UnkStruct_RwV3dView field_124[8];  // [C] the eight frustum corners: 0..3 near, 4..7 far ([L] frustumCorners)
};

// [C] the sign byte stored with a plane normal component: (the float's bits >> 31, arithmetic) + 1.
static inline unsigned char func_001E0B48_inl1(float f)
{
    union {
        float f;
        int i;
    } u;

    u.f = f;
    return (unsigned char)((u.i >> 31) + 1);
}

// [C] stores the normal (nx, ny, nz) in the plane, d = (pt.x * nx + pt.y * ny) + pt.z * nz and the sign bytes.
static inline void func_001E0B48_inl2(UnkStruct_RwPlaneView *p, const UnkStruct_RwV3dView *pt, float nx, float ny, float nz)
{
    float f3, f4, f5;

    p->nx = nx;
    p->ny = ny;
    p->nz = nz;
    f3 = pt->x * nx;
    f4 = pt->y * ny;
    f5 = pt->z * nz;
    f3 = f3 + f4;
    f3 = f3 + f5;
    p->sx = func_001E0B48_inl1(nx);
    p->sy = func_001E0B48_inl1(ny);
    p->sz = func_001E0B48_inl1(nz);
    p->d = f3;
}

// [C] the normal (x, y, z) is scaled by the reciprocal of its length (sqrt of (x*x + y*y) + z*z; a length that is not
// > 0 is used as the factor itself), then stored with func_001E0B48_inl2.
static inline void func_001E0B48_inl3(UnkStruct_RwPlaneView *p, const UnkStruct_RwV3dView *pt, float x, float y, float z)
{
    float f0, f1, f2, f3;

    f1 = x * x;
    f0 = y * y;
    f2 = z * z;
    f1 = f1 + f0;
    f1 = f1 + f2;
    f1 = sqrtf(f1);
    f3 = f1;
    if (0.0f < f1) {
        f0 = 1.0f;
        f3 = f0 / f1;
    }
    x = x * f3;
    y = y * f3;
    z = z * f3;
    func_001E0B48_inl2(p, pt, x, y, z);
}

// [C] cross product a x b: x = ay*bz - az*by, y = az*bx - ax*bz, z = ax*by - ay*bx (each product rounded first).
static inline void func_001E0B48_inl4(const UnkStruct_RwV3dView *a, const UnkStruct_RwV3dView *b, float *x, float *y, float *z)
{
    float p0 = a->y * b->z;
    float p1 = a->y * b->x;
    float p2 = a->x * b->y;
    float p3 = a->z * b->x;
    float p4 = a->x * b->z;
    float p5 = a->z * b->y;

    *z = p2 - p1;
    *y = p3 - p4;
    *x = p0 - p5;
}

// [C] the perspective view frustum: with L = the camera frame's LTM (+0x50: right, up, at, pos rows) and the camera's
// view window w (+0x68 / +0x6C), view offset o (+0x78 / +0x7C), near n (+0x80) and far f (+0x84): the near corners
// 0..3 (also kept in the t40-relative form) are L.at + (+-w.x * L.right) + (+-w.y * L.up) for the eye offset
// t40 = -o.x * L.right + o.y * L.up, each corner = t40 + pos + (corner - t40) * n for the near ring and * f for the far
// ring (corners 4..7); then the six planes: 0 = L.at with d from corner 4, 1 = its negation with d from corner 0, 2..5
// = the side planes from cross products of the corner edges, each normalised. [G] the plane order (near/far/left/...).
// The sign bytes are (bits >> 31) + 1 of the stored normal components (so -0.0 counts as negative). The stack
// temporaries (sp+0 .. 0x48) are kept as locals.
// 0x001E0B48
extern "C" void CameraBuildPerspClipPlanes(UnkStruct_RwCameraSync *camera)
{
    UnkStruct_RwV3dView t0, t10, t40;
    UnkStruct_RwV3dView *corner = camera->field_124;
    UnkStruct_RwPlaneView *plane = camera->field_94;
    const float *L = (const float *)(camera->field_4 + 0x50);
    float f0, f1, f2, f3, f4, f5, f6, f7, f8, f9;
    float nx, ny, nz;
    int i;

    f0 = camera->field_78;
    f0 = -f0;
    f1 = L[0] * f0;
    t40.x = f1;
    f2 = L[1] * f0;
    t40.y = f2;
    f3 = L[2] * f0;
    t40.z = f3;
    f0 = L[4];
    f4 = camera->field_7C;
    f0 = f0 * f4;
    f1 = f1 + f0;
    t40.x = f1;
    f0 = L[5];
    f0 = f0 * f4;
    f2 = f2 + f0;
    t40.y = f2;
    f0 = L[6];
    f0 = f0 * f4;
    f3 = f3 + f0;
    t40.z = f3;
    f9 = L[0];
    f0 = camera->field_68;
    f9 = f9 * f0;
    f8 = L[1];
    f8 = f8 * f0;
    f7 = L[2];
    f7 = f7 * f0;
    f3 = L[8];
    f3 = f3 + f9;
    t0.x = f3;
    f2 = L[9];
    f2 = f2 + f8;
    t0.y = f2;
    f1 = L[10];
    f1 = f1 + f7;
    t0.z = f1;
    f6 = L[4];
    f0 = camera->field_6C;
    f6 = f6 * f0;
    f4 = L[5];
    f4 = f4 * f0;
    f5 = L[6];
    f5 = f5 * f0;
    f3 = f3 + f6;
    f2 = f2 + f4;
    t0.x = f3;
    f1 = f1 + f5;
    t0.y = f2;
    t0.z = f1;
    f9 = f9 + f9;
    f8 = f8 + f8;
    f7 = f7 + f7;
    f6 = f6 + f6;
    f4 = f4 + f4;
    f5 = f5 + f5;
    corner[0] = t0;
    f3 = f3 - f9;
    f2 = f2 - f8;
    t0.x = f3;
    f1 = f1 - f7;
    t0.y = f2;
    t0.z = f1;
    corner[1] = t0;
    f3 = f3 - f6;
    f2 = f2 - f4;
    t0.x = f3;
    f1 = f1 - f5;
    t0.y = f2;
    t0.z = f1;
    corner[2] = t0;
    f3 = f3 + f9;
    f2 = f2 + f8;
    t0.x = f3;
    f1 = f1 + f7;
    t0.y = f2;
    t0.z = f1;
    corner[3] = t0;
    for (i = 0; i < 4; i++) {
        UnkStruct_RwV3dView *v0 = &corner[i];

        f1 = v0->x;
        f4 = t40.x;
        f2 = t40.y;
        f1 = f1 - f4;
        f3 = t40.z;
        t0.x = f1;
        f0 = v0->y;
        f0 = f0 - f2;
        t0.y = f0;
        f1 = v0->z;
        f1 = f1 - f3;
        t0.z = f1;
        f0 = L[12];
        f4 = f4 + f0;
        v0->x = f4;
        f0 = L[13];
        f2 = t40.y;
        f2 = f2 + f0;
        v0->y = f2;
        f3 = t40.z;
        f0 = L[14];
        f3 = f3 + f0;
        v0->z = f3;
        f0 = t0.x;
        f1 = camera->field_80;
        f0 = f0 * f1;
        f4 = f4 + f0;
        v0->x = f4;
        f1 = camera->field_80;
        f0 = t0.y;
        f0 = f0 * f1;
        f2 = f2 + f0;
        v0->y = f2;
        f0 = t0.z;
        f1 = camera->field_80;
        f0 = f0 * f1;
        f3 = f3 + f0;
        v0->z = f3;
        v0 = &corner[i + 4];
        f2 = t40.x;
        f0 = L[12];
        f2 = f2 + f0;
        v0->x = f2;
        f0 = L[13];
        f3 = t40.y;
        f3 = f3 + f0;
        v0->y = f3;
        f4 = t40.z;
        f0 = L[14];
        f4 = f4 + f0;
        v0->z = f4;
        f0 = t0.x;
        f1 = camera->field_84;
        f0 = f0 * f1;
        f2 = f2 + f0;
        v0->x = f2;
        f1 = camera->field_84;
        f0 = t0.y;
        f0 = f0 * f1;
        f3 = f3 + f0;
        v0->y = f3;
        f0 = t0.z;
        f1 = camera->field_84;
        f0 = f0 * f1;
        f4 = f4 + f0;
        v0->z = f4;
    }
    // plane 0: the frame's at row, d from corner 4
    func_001E0B48_inl2(&plane[0], &corner[4], L[8], L[9], L[10]);
    // plane 1: its negation, d from corner 0
    f0 = plane[0].nx;
    f1 = plane[0].ny;
    f0 = -f0;
    f2 = plane[0].nz;
    f1 = -f1;
    f2 = -f2;
    func_001E0B48_inl2(&plane[1], &corner[0], f0, f1, f2);
    // plane 2: t0 = corner 1 - corner 5, t10 = corner 6 - corner 5
    f7 = corner[1].x;
    f2 = corner[5].x;
    f6 = corner[1].y;
    f8 = corner[5].y;
    f7 = f7 - f2;
    f5 = corner[1].z;
    f1 = corner[5].z;
    f6 = f6 - f8;
    t0.x = f7;
    f5 = f5 - f1;
    t0.y = f6;
    t0.z = f5;
    f0 = corner[6].x;
    f4 = corner[6].y;
    f0 = f0 - f2;
    f3 = corner[6].z;
    f4 = f4 - f8;
    f3 = f3 - f1;
    t10.x = f0;
    t10.y = f4;
    t10.z = f3;
    func_001E0B48_inl4(&t0, &t10, &nx, &ny, &nz);
    func_001E0B48_inl3(&plane[2], &corner[1], nx, ny, nz);
    // plane 3: t0 is kept from plane 2; t10 = corner 4 - corner 5 (all three components)
    f2 = corner[4].x;
    f0 = corner[5].x;
    f4 = corner[4].y;
    f2 = f2 - f0;
    f1 = corner[5].y;
    f3 = corner[4].z;
    f0 = corner[5].z;
    f4 = f4 - f1;
    t10.x = f2;
    f3 = f3 - f0;
    t10.y = f4;
    t10.z = f3;
    func_001E0B48_inl4(&t10, &t0, &nx, &ny, &nz);
    func_001E0B48_inl3(&plane[3], &corner[1], nx, ny, nz);
    // plane 4: t0 = corner 3 - corner 7, t10 = corner 4 - corner 7
    f7 = corner[3].x;
    f2 = corner[7].x;
    f6 = corner[3].y;
    f8 = corner[7].y;
    f7 = f7 - f2;
    f5 = corner[3].z;
    f1 = corner[7].z;
    f6 = f6 - f8;
    t0.x = f7;
    f5 = f5 - f1;
    t0.y = f6;
    t0.z = f5;
    f0 = corner[4].x;
    f4 = corner[4].y;
    f0 = f0 - f2;
    f3 = corner[4].z;
    f4 = f4 - f8;
    f3 = f3 - f1;
    t10.x = f0;
    t10.y = f4;
    t10.z = f3;
    func_001E0B48_inl4(&t0, &t10, &nx, &ny, &nz);
    func_001E0B48_inl3(&plane[4], &corner[3], nx, ny, nz);
    // plane 5: t0 is kept from plane 4; t10 = corner 6 - corner 7
    f2 = corner[6].x;
    f0 = corner[7].x;
    f4 = corner[6].y;
    f2 = f2 - f0;
    f1 = corner[7].y;
    f3 = corner[6].z;
    f0 = corner[7].z;
    f4 = f4 - f1;
    t10.x = f2;
    f3 = f3 - f0;
    t10.y = f4;
    t10.z = f3;
    func_001E0B48_inl4(&t10, &t0, &nx, &ny, &nz);
    func_001E0B48_inl3(&plane[5], &corner[3], nx, ny, nz);
}

// [C] the perspective view matrix. With L = the camera frame's LTM (frame + 0x50), w = the reciprocal view window
// (+0x70 / +0x74), o = the view offset (+0x78 / +0x7C), h = 0.5, m = the view matrix (camera + 0x20):
// column 0: g = -0.5 * w.x; m[0,4,8] = g * L.right + (h - g * o.x) * L.at, m[12] = h - ((L.pos . that column) + (h - g*o.x));
// column 1: g = -0.5 * w.y; m[1,5,9] = g * L.up + (g * o.y + h) * L.at, m[13] = h - ((L.pos . that column) + (g*o.y + h));
// column 2: m[2,6,10] = L.at, m[14] = -(L.at . L.pos) (summed as x*x + y*y + z*z order of the asm);
// then RwMatrixOptimize(m, NULL) (the default tolerances). Returns the camera.
// 0x001E1410
extern "C" void *CameraBuildPerspViewMatrix(UnkStruct_RwCameraSync *camera)
{
    float *m = camera->field_20;
    const float *L = (const float *)(camera->field_4 + 0x50);
    float f0, f1, f2, f3, f4, f5, f6, f7, f8;

    f7 = -0.5f;
    f0 = camera->field_70;
    f8 = f0 * f7;
    f4 = L[0];
    f4 = f4 * f8;
    f3 = L[1];
    f3 = f3 * f8;
    f2 = L[2];
    f2 = f2 * f8;
    f1 = camera->field_78;
    f6 = 0.5f;
    f1 = f8 * f1;
    f8 = f6 - f1;
    f0 = L[8];
    f0 = f0 * f8;
    f4 = f4 + f0;
    f0 = L[9];
    f0 = f0 * f8;
    f3 = f3 + f0;
    f0 = L[10];
    f0 = f0 * f8;
    f2 = f2 + f0;
    m[0] = f4;
    m[4] = f3;
    m[8] = f2;
    f0 = L[12];
    f5 = L[13];
    f0 = f0 * f4;
    f1 = L[14];
    f5 = f5 * f3;
    f1 = f1 * f2;
    f0 = f0 + f5;
    f0 = f0 + f1;
    f0 = f8 + f0;
    f0 = f6 - f0;
    m[12] = f0;
    f1 = camera->field_74;
    f8 = f1 * f7;
    f4 = L[4];
    f4 = f4 * f8;
    f3 = L[5];
    f3 = f3 * f8;
    f2 = L[6];
    f2 = f2 * f8;
    f1 = camera->field_7C;
    f1 = f8 * f1;
    f8 = f1 + f6;
    f0 = L[8];
    f0 = f0 * f8;
    f4 = f4 + f0;
    f0 = L[9];
    f0 = f0 * f8;
    f3 = f3 + f0;
    f0 = L[10];
    f0 = f0 * f8;
    f2 = f2 + f0;
    m[1] = f4;
    m[5] = f3;
    m[9] = f2;
    f0 = L[12];
    f5 = L[13];
    f0 = f0 * f4;
    f1 = L[14];
    f5 = f5 * f3;
    f1 = f1 * f2;
    f0 = f0 + f5;
    f0 = f0 + f1;
    f0 = f8 + f0;
    f6 = f6 - f0;
    m[13] = f6;
    f0 = L[8];
    m[2] = f0;
    f1 = L[9];
    m[6] = f1;
    f0 = L[10];
    m[10] = f0;
    f0 = L[8];
    f1 = L[12];
    f2 = L[13];
    f3 = L[9];
    f1 = f1 * f0;
    f4 = L[10];
    f2 = f2 * f3;
    f0 = L[14];
    f0 = f0 * f4;
    f1 = f1 + f2;
    f1 = f1 + f0;
    f1 = -f1;
    m[14] = f1;
    RwMatrixOptimize(m, 0);
    return camera;
}

// [C] the parallel-projection view matrix: as CameraBuildPerspViewMatrix with the offsets in the other sign: column 0
// g = -0.5 * w.x, a = -(g * o.x), m[0,4,8] = g * L.right + a * L.at, m[12] = 0.5 - ((L.pos . column) + a); column 1:
// g = -0.5 * w.y, b = g * o.y, m[1,5,9] = g * L.up + b * L.at, m[13] = 0.5 - ((L.pos . column) + b); column 2 as in
// the perspective one; then RwMatrixOptimize(m, NULL). Returns the camera.
// 0x001E1B28
extern "C" void *CameraBuildParallelViewMatrix(UnkStruct_RwCameraSync *camera)
{
    float *m = camera->field_20;
    const float *L = (const float *)(camera->field_4 + 0x50);
    float f0, f1, f2, f3, f4, f5, f6, f7, f8;

    f7 = -0.5f;
    f0 = camera->field_70;
    f8 = f0 * f7;
    f4 = L[0];
    f4 = f4 * f8;
    f3 = L[1];
    f3 = f3 * f8;
    f2 = L[2];
    f2 = f2 * f8;
    f1 = camera->field_78;
    f1 = f8 * f1;
    f8 = -f1;
    f0 = L[8];
    f0 = f0 * f8;
    f4 = f4 + f0;
    f0 = L[9];
    f0 = f0 * f8;
    f3 = f3 + f0;
    f0 = L[10];
    f0 = f0 * f8;
    f2 = f2 + f0;
    m[0] = f4;
    f6 = 0.5f;
    m[4] = f3;
    m[8] = f2;
    f0 = L[12];
    f5 = L[13];
    f0 = f0 * f4;
    f1 = L[14];
    f5 = f5 * f3;
    f1 = f1 * f2;
    f0 = f0 + f5;
    f0 = f0 + f1;
    f0 = f8 + f0;
    f0 = f6 - f0;
    m[12] = f0;
    f1 = camera->field_74;
    f8 = f1 * f7;
    f3 = L[4];
    f3 = f3 * f8;
    f2 = L[5];
    f2 = f2 * f8;
    f1 = L[6];
    f1 = f1 * f8;
    f4 = camera->field_7C;
    f8 = f8 * f4;
    f0 = L[8];
    f0 = f0 * f8;
    f3 = f3 + f0;
    f0 = L[9];
    f0 = f0 * f8;
    f2 = f2 + f0;
    f0 = L[10];
    f0 = f0 * f8;
    f1 = f1 + f0;
    m[1] = f3;
    m[5] = f2;
    m[9] = f1;
    f0 = L[12];
    f5 = L[13];
    f0 = f0 * f3;
    f4 = L[14];
    f5 = f5 * f2;
    f4 = f4 * f1;
    f0 = f0 + f5;
    f0 = f0 + f4;
    f0 = f8 + f0;
    f6 = f6 - f0;
    m[13] = f6;
    f0 = L[8];
    m[2] = f0;
    f1 = L[9];
    m[6] = f1;
    f0 = L[10];
    m[10] = f0;
    f0 = L[8];
    f1 = L[12];
    f2 = L[13];
    f3 = L[9];
    f1 = f1 * f0;
    f4 = L[10];
    f2 = f2 * f3;
    f0 = L[14];
    f0 = f0 * f4;
    f1 = f1 + f2;
    f1 = f1 + f0;
    f1 = -f1;
    m[14] = f1;
    RwMatrixOptimize(m, 0);
    return camera;
}

// [C] the parallel-projection frustum. With near n (+0x80), far f (+0x84), view window w (+0x68 / +0x6C) and view
// offset o (+0x78 / +0x7C): corner z = n for corners 0..3 and f for 4..7; x: corners 0 and 3 = w.x + (1 - n) * -o.x,
// corners 1 and 2 = -w.x + (1 - n) * -o.x, corners 7 and 4 = w.x + (1 - f) * -o.x, corners 6 and 5 = -w.x + (1 - f) *
// -o.x; y: corners 1 and 0 = w.y + (1 - n) * o.y, corners 3 and 2 = -w.y + (1 - n) * o.y, corners 5 and 4 = w.y +
// (1 - f) * o.y, corners 7 and 6 = -w.y + (1 - f) * o.y. The eight corners are then transformed by the frame's LTM
// (RwV3dTransformPoints(corners, corners, 8, LTM)); the planes: 0 = L.at (LTM + 0x20, three words) with d from
// corner 4, 1 = its negation with d from corner 0, 2 and 3 the side planes (cross products of corner edges, as in
// the perspective build), 4 and 5 = the negations of planes 2 and 3 with d from corner 3. Sign bytes as in
// CameraBuildPerspClipPlanes.
// 0x001E15E8
extern "C" void CameraBuildParallelClipPlanes(UnkStruct_RwCameraSync *camera)
{
    UnkStruct_RwV3dView t0, t10;
    UnkStruct_RwV3dView *corner = camera->field_124;
    UnkStruct_RwPlaneView *plane = camera->field_94;
    unsigned char *frame;
    const float *L;
    float f0, f1, f2, f3, f4, f5, f6, f7, f8, f9, f11, f20;
    float nx, ny, nz;

    f20 = 1.0f;
    f1 = camera->field_80;
    f2 = camera->field_78;
    f8 = f20 - f1;
    f3 = camera->field_68;
    f2 = -f2;
    f5 = camera->field_6C;
    f0 = camera->field_84;
    f4 = -f3;
    {
        float f10 = camera->field_7C;

        corner[3].z = f1;
        f11 = f8 * f2;
        corner[2].z = f1;
        f9 = f20 - f0;
        corner[1].z = f1;
        corner[0].z = f1;
        f7 = f4 + f11;
        f6 = f3 + f11;
        corner[7].z = f0;
        f11 = f9 * f2;
        corner[6].z = f0;
        corner[5].z = f0;
        f1 = -f5;
        corner[4].z = f0;
        f4 = f4 + f11;
        corner[0].x = f6;
        f3 = f3 + f11;
        corner[3].x = f6;
        corner[2].x = f7;
        f11 = f8 * f10;
        corner[1].x = f7;
        corner[7].x = f3;
        corner[4].x = f3;
        f2 = f1 + f11;
        f0 = f5 + f11;
        corner[6].x = f4;
        f11 = f9 * f10;
        corner[5].x = f4;
        frame = camera->field_4;
        corner[1].y = f0;
        corner[0].y = f0;
        f5 = f5 + f11;
        corner[3].y = f2;
        f1 = f1 + f11;
        corner[2].y = f2;
        L = (const float *)(frame + 0x50);
        corner[5].y = f5;
        corner[4].y = f5;
        corner[7].y = f1;
        corner[6].y = f1;
    }
    RwV3dTransformPoints(corner, corner, 8, L);
    // plane 0: the frame's at row (copied through memory), d from corner 4
    func_001E0B48_inl2(&plane[0], &corner[4], *(const float *)(frame + 0x70), *(const float *)(frame + 0x74),
                       *(const float *)(frame + 0x78));
    // plane 1: its negation, d from corner 0
    f0 = plane[0].nx;
    f0 = -f0;
    f4 = plane[0].ny;
    f4 = -f4;
    f2 = plane[0].nz;
    f2 = -f2;
    func_001E0B48_inl2(&plane[1], &corner[0], f0, f4, f2);
    // plane 2: t0 = corner 1 - corner 5, t10 = corner 6 - corner 5
    f7 = corner[1].x;
    f2 = corner[5].x;
    f6 = corner[1].y;
    f8 = corner[5].y;
    f7 = f7 - f2;
    f5 = corner[1].z;
    f1 = corner[5].z;
    f6 = f6 - f8;
    t0.x = f7;
    f5 = f5 - f1;
    t0.y = f6;
    t0.z = f5;
    f0 = corner[6].x;
    f4 = corner[6].y;
    f0 = f0 - f2;
    f3 = corner[6].z;
    f4 = f4 - f8;
    f3 = f3 - f1;
    t10.x = f0;
    t10.y = f4;
    t10.z = f3;
    func_001E0B48_inl4(&t0, &t10, &nx, &ny, &nz);
    func_001E0B48_inl3(&plane[2], &corner[1], nx, ny, nz);
    // plane 3: t0 is kept; t10 = corner 4 - corner 5
    f2 = corner[4].x;
    f0 = corner[5].x;
    f4 = corner[4].y;
    f2 = f2 - f0;
    f1 = corner[5].y;
    f3 = corner[4].z;
    f0 = corner[5].z;
    f4 = f4 - f1;
    t10.x = f2;
    f3 = f3 - f0;
    t10.y = f4;
    t10.z = f3;
    func_001E0B48_inl4(&t10, &t0, &nx, &ny, &nz);
    func_001E0B48_inl3(&plane[3], &corner[1], nx, ny, nz);
    // plane 4: the negation of plane 2's normal, d from corner 3
    f0 = plane[2].nx;
    f1 = plane[2].ny;
    f0 = -f0;
    f2 = plane[2].nz;
    f1 = -f1;
    f2 = -f2;
    func_001E0B48_inl2(&plane[4], &corner[3], f0, f1, f2);
    // plane 5: the negation of plane 3's normal, d from corner 3
    f4 = plane[3].nx;
    f5 = plane[3].ny;
    f6 = plane[3].nz;
    f4 = -f4;
    f5 = -f5;
    f6 = -f6;
    func_001E0B48_inl2(&plane[5], &corner[3], f4, f5, f6);
}

// [C] the camera's sync function (installed at +0x10 by RwCameraCreate): projection (+0x14) == 1 -> the perspective
// view matrix and clip planes, anything else -> the parallel ones; then RwBBoxCalculate(camera + 0x10C, camera + 0x124,
// 8) (the bounding box of the eight frustum corners). Returns the camera.
// 0x001E2610
extern "C" int CameraSync(void *cam)
{
    UnkStruct_RwCameraSync *camera = (UnkStruct_RwCameraSync *)cam;

    if (camera->field_14 == 1) {
        CameraBuildPerspViewMatrix(camera);
        CameraBuildPerspClipPlanes(camera);
    } else {
        CameraBuildParallelViewMatrix(camera);
        CameraBuildParallelClipPlanes(camera);
    }
    RwBBoxCalculate(camera->field_10C, camera->field_124, 8);
    return (int)camera;
}

// [C] the RW globals' words the camera update uses.
struct UnkStruct_ourGlobalsUpdate {
    void *field_0;                     // [C] the camera being updated ([L] currentCamera): set by begin, cleared by end
    unsigned char pad_4[0x48];
    int (*field_4C)(unsigned int, void *, unsigned int);    // [C] the standard function called by begin (a0 = 0, a1 = camera, a2 = 0) ([G] camera begin update request)
    unsigned char pad_50[0x20];
    int (*field_70)(unsigned int, void *, unsigned int);    // [C] the standard function called by end (a0 = 0, a1 = camera, a2 = 0) ([G] camera end update request)
};

// [C] calls the function at ourGlobals + 0x70 with (0, camera, 0); when it answers nonzero ourGlobals + 0 (the current
// camera) is cleared and the camera is returned, else 0 (the current camera stays).
// 0x001E2680
extern "C" int CameraEndUpdate(void *cam)
{
    UnkStruct_ourGlobalsUpdate *g = (UnkStruct_ourGlobalsUpdate *)ourGlobals;

    if (g->field_70(0, cam, 0) != 0) {
        g->field_0 = 0;
        return (int)cam;
    }
    return 0;
}

// [C] ourGlobals + 0 (the current camera) = camera; _rwFrameSyncDirty(); calls the function at ourGlobals + 0x4C with
// (0, camera, 0); when it answers 0 returns 0, else _rwPipeInitForCamera(camera) and returns the camera.
// 0x001E26D8
extern "C" int CameraBeginUpdate(void *cam)
{
    UnkStruct_ourGlobalsUpdate *g = (UnkStruct_ourGlobalsUpdate *)ourGlobals;

    g->field_0 = cam;
    _rwFrameSyncDirty();
    if (g->field_4C(0, cam, 0) == 0) {
        return 0;
    }
    _rwPipeInitForCamera(cam);
    return (int)cam;
}
