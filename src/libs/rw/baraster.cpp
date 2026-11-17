// RenderWare 3.6 core (RW 3.1 file name: src/baraster.c; reading aid only).
// Written from the retail asm (decomp assembly/asm/rw/renderware.s). Tags: [C] confirmed from the asm, [L], [G].
// boot1 staging (rw_wip).

extern unsigned char rasterTKList[];   // [C] the plugin registry (0x18 bytes, symbol table) ([L] RwPluginRegistry)

// [C] the standard functions (filled by device request 0xB from ourGlobals + 0x48, 0x1D entries) and the memory
// functions; RwStandardFunc shape (out, inOut, numIn) [L 3.1].
struct UnkStruct_ourGlobals {
    unsigned char pad_0[0x58];
    int (*field_58)(void *, void *, int);       // [C] entry 4, called (0, raster, flags) ([L] rwSTANDARDRASTERCREATE)
    int (*field_5C)(void *, void *, int);       // [C] entry 5, called (0, raster, 0) ([L] rwSTANDARDRASTERDESTROY)
    unsigned char pad_60[0xE4];
    void *(*field_144)(void *, unsigned int);   // [C] (free list, hint) ([L] memoryAlloc)
    void *(*field_148)(void *, void *);         // [C] (free list, block) ([L] memoryFree)
};
extern UnkStruct_ourGlobals ourGlobals;
extern int rasterModule;               // [C] gp-relative 0x004E27B0 (8 bytes): +0 the raster globals' offset in ourGlobals

// [C] the raster globals at ourGlobals + rasterModule ([L] rwRasterGlobals).
struct UnkStruct_rasterModule {
    unsigned char pad_0[0x60];
    void *field_60;                             // [C] the raster free list ([L] rasterFreeList)
};

// [C] a raster ([L] RwRaster; 3.1 field order, 3.6 sizes not checked here).
struct UnkStruct_RwRasterCreate {
    UnkStruct_RwRasterCreate *field_0;          // [C] set to the raster itself ([L] parent)
    unsigned int field_4;                       // [C] 0 ([L] cpPixels)
    unsigned int field_8;                       // [C] 0 ([L] palette)
    int field_C;                                // [C] width
    int field_10;                               // [C] height
    int field_14;                               // [C] depth
    unsigned char pad_18[0x4];
    short field_1C;                             // [C] 0 ([L] nOffsetX)
    short field_1E;                             // [C] 0 ([L] nOffsetY)
    unsigned char pad_20[0x1];
    unsigned char field_21;                     // [C] 0 ([L] cFlags)
    unsigned char field_22;                     // [C] 0 ([L] privateFlags)
};

extern "C" void *_rwPluginRegistryInitObject(void *registry, void *object);
extern "C" void *_rwPluginRegistryDeInitObject(void *registry, void *object);

extern "C" int _rwPluginRegistryAddPlugin(void *registry, int size, unsigned int pluginID, void *constructCB,
                                          void *destructCB, void *copyCB);

// [C] a call of _rwPluginRegistryAddPlugin(&rasterTKList, size, pluginID, constructCB, destructCB, copyCB), all five
// arguments passed on ([L] copyCB: the sixth argument lands in the entry's +0x28).
// 0x001E7B40
extern "C" int RwRasterRegisterPlugin(int size, unsigned int pluginID, void *constructCB, void *destructCB, void *copyCB)
{
    return _rwPluginRegistryAddPlugin(rasterTKList, size, pluginID, constructCB, destructCB, copyCB);
}

// [C] a raster from the raster free list (ourGlobals + 0x144, hint 0x30407); NULL when that fails. Width, height,
// depth, then bytes +0x22 / +0x21, halves +0x1C / +0x1E, parent = itself, +4 and +8 cleared, in that order; then the
// standard raster-create function (0, raster, flags). A 0 answer gives the raster back to the free list
// (ourGlobals + 0x148) and returns NULL; otherwise the raster plugins' constructors run (their result is ignored)
// and the raster is returned.
// 0x001E74F0
extern "C" UnkStruct_RwRasterCreate *RwRasterCreate(int width, int height, int depth, int flags)
{
    UnkStruct_RwRasterCreate *raster;

    raster = (UnkStruct_RwRasterCreate *)ourGlobals.field_144(
        ((UnkStruct_rasterModule *)((unsigned char *)&ourGlobals + rasterModule))->field_60, 0x30407);
    if (raster == 0) {
        return 0;
    }
    raster->field_C = width;
    raster->field_10 = height;
    raster->field_14 = depth;
    raster->field_22 = 0;
    raster->field_21 = 0;
    raster->field_1C = 0;
    raster->field_1E = 0;
    raster->field_0 = raster;
    raster->field_4 = 0;
    raster->field_8 = 0;
    if (ourGlobals.field_58(0, raster, flags) == 0) {
        ourGlobals.field_148(((UnkStruct_rasterModule *)((unsigned char *)&ourGlobals + rasterModule))->field_60,
                             raster);
        return 0;
    }
    _rwPluginRegistryInitObject(rasterTKList, raster);
    return raster;
}

// [C] the raster plugins' destructors, the standard raster-destroy function (0, raster, 0), then the raster goes back
// to the raster free list (ourGlobals + 0x148). Returns 1.
// 0x001E75F0
extern "C" int RwRasterDestroy(UnkStruct_RwRasterCreate *raster)
{
    _rwPluginRegistryDeInitObject(rasterTKList, raster);
    ourGlobals.field_5C(0, raster, 0);
    ourGlobals.field_148(((UnkStruct_rasterModule *)((unsigned char *)&ourGlobals + rasterModule))->field_60, raster);
    return 1;
}

extern int D_004E27B4;                 // [C] gp-relative 0x004E27B4 = rasterModule + 4: the module's open count
extern int _rwRasterFreeListBlockSize;           // [C] gp-relative
extern int _rwRasterFreeListPreallocBlocks;      // [C] gp-relative
extern unsigned char D_00674C48[];     // [C] in-place space for the raster free list

extern "C" void *RwFreeListCreateAndPreallocateSpace(int entrySize, int entriesPerBlock, int alignment,
                                                     int numBlocksToPreallocate, void *inPlaceSpace, unsigned int hint);
extern "C" int RwFreeListDestroy(void *freeList);
extern "C" void *memset(void *dst, int value, unsigned int count);

// [C] the raster globals as _rwRasterOpen initialises them; +0x2C..+0x5F is cleared first.
struct UnkStruct_rasterModuleOpen {
    unsigned char *field_0;            // [C] = this + 0x2C ([L] a pointer to the cleared block)
    unsigned char pad_4[0x24];
    int field_28;                      // [C] 0
    unsigned char pad_2C[0x4];
    int field_30;                      // [C] 0
    int field_34;                      // [C] 0
    int field_38;                      // [C] 0
    int field_3C;                      // [C] 0
    int field_40;                      // [C] 0
    unsigned char pad_44[0x8];
    unsigned char field_4C;            // [C] 0
    unsigned char field_4D;            // [C] 0x80
    unsigned char pad_4E[0x12];
    void *field_60;                    // [C] the raster free list (entries of the first word of rasterTKList, alignment 4)
};

// [C] rasterModule = offset; memset(base + 0x2C, 0, 0x34); then base +0x4D = 0x80, +0 = base + 0x2C, +0x38 = +0x3C =
// +0x40 = +0x30 = +0x34 = 0, +0x4C = 0, +0x28 = 0 (all stored before the next call); +0x60 =
// RwFreeListCreateAndPreallocateSpace(first word of rasterTKList, _rwRasterFreeListBlockSize, 4,
// _rwRasterFreeListPreallocBlocks, D_00674C48, 0x40407); 0 -> returns 0; else open count += 1 and returns the instance.
// 0x001E7408
extern "C" void *_rwRasterOpen(void *instance, int offset, int size)
{
    UnkStruct_rasterModuleOpen *g;
    void *freeList;

    rasterModule = offset;
    memset((unsigned char *)&ourGlobals + offset + 0x2C, 0, 0x34);
    g = (UnkStruct_rasterModuleOpen *)((unsigned char *)&ourGlobals + rasterModule);
    g->field_4D = 0x80;
    g->field_0 = (unsigned char *)&ourGlobals + rasterModule + 0x2C;
    g->field_38 = 0;
    g->field_3C = 0;
    g->field_40 = 0;
    g->field_30 = 0;
    g->field_34 = 0;
    g->field_4C = 0;
    g->field_28 = 0;
    freeList = RwFreeListCreateAndPreallocateSpace(*(int *)rasterTKList, _rwRasterFreeListBlockSize, 4,
                                                   _rwRasterFreeListPreallocBlocks, D_00674C48, 0x40407);
    g = (UnkStruct_rasterModuleOpen *)((unsigned char *)&ourGlobals + rasterModule);
    g->field_60 = freeList;
    if (freeList == 0) {
        return 0;
    }
    D_004E27B4 = D_004E27B4 + 1;
    return instance;
}

// [C] if the free list (+0x60) is set: RwFreeListDestroy it and clear it; open count -= 1; returns the instance.
// 0x001E7BB8
extern "C" void *_rwRasterClose(void *instance, int offset, int size)
{
    void *freeList = ((UnkStruct_rasterModuleOpen *)((unsigned char *)&ourGlobals + rasterModule))->field_60;

    if (freeList != 0) {
        RwFreeListDestroy(freeList);
        ((UnkStruct_rasterModuleOpen *)((unsigned char *)&ourGlobals + rasterModule))->field_60 = 0;
    }
    D_004E27B4 = D_004E27B4 - 1;
    return instance;
}

// ---------------------------------------------------------------------------------------------------------------
// boot2 additions (track rw): sub-rasters and show-raster.

// [C] the sub-raster's rectangle as RwRasterSubRaster reads it ([L] RwRect: x, y, w, h as ints; only +0, +4, +8, +0xC
// are read).
struct UnkStruct_RwRect {
    unsigned short field_0;            // [C] x (lhu) ([L] x)
    unsigned char pad_2[0x2];
    unsigned short field_4;            // [C] y (lhu)
    unsigned char pad_6[0x2];
    int field_8;                       // [C] width
    int field_C;                       // [C] height
};

// [C] a raster as RwRasterSubRaster fills it (same layout as UnkStruct_RwRasterCreate, flags byte at +0x21).
struct UnkStruct_RwRasterSub {
    UnkStruct_RwRasterSub *field_0;    // [C] parent
    unsigned char pad_4[0x8];
    int field_C;                       // [C] width
    int field_10;                      // [C] height
    unsigned char pad_14[0x8];
    unsigned short field_1C;           // [C] nOffsetX (lhu / sh)
    unsigned short field_1E;           // [C] nOffsetY
    unsigned char pad_20[0x1];
    unsigned char field_21;            // [C] cFlags (bit 7 = may be sub-rastered)
};

extern "C" void _rwResourcesPurge(void);

struct UnkStruct_ourGlobalsRaster {
    unsigned char pad_0[0x78];
    int (*field_78)(void *, void *, int);   // [C] standard raster sub-raster function (out 0 -> raster, parent, 0)
    unsigned char pad_7C[0x1C];
    int (*field_98)(void *, void *, unsigned int);   // [C] standard show-raster function (raster, dev, flags)
};

// [C] only when the raster's (a0) +0x21 bit 7 is set (else NULL): raster width / height = rect +8 / +0xC, offsets
// (+0x1C / +0x1E) = the parent's (a1) + rect x / y (unsigned halfword adds), then the standard function ourGlobals + 0x78
// is called with (raster, parent, 0). A 0 answer returns NULL; else the raster's +0 = the parent's +0 (the parent's
// root) and the raster is returned.
// 0x001E76C8
extern "C" UnkStruct_RwRasterSub *RwRasterSubRaster(UnkStruct_RwRasterSub *raster, UnkStruct_RwRasterSub *parent,
                                                    UnkStruct_RwRect *rect)
{
    UnkStruct_ourGlobalsRaster *g = (UnkStruct_ourGlobalsRaster *)&ourGlobals;
    UnkStruct_RwRasterSub *root;
    unsigned int x;
    unsigned int y;

    if ((raster->field_21 & 0x80) == 0) {
        return 0;
    }
    raster->field_C = rect->field_8;
    x = parent->field_1C;
    raster->field_10 = rect->field_C;
    x = x + rect->field_0;
    raster->field_1C = (unsigned short)x;
    y = rect->field_4;
    y = parent->field_1E + y;
    raster->field_1E = (unsigned short)y;   // [C] stored in the call's delay slot, before the call
    if (g->field_78(raster, parent, 0) == 0) {
        return 0;
    }
    root = parent->field_0;
    raster->field_0 = root;
    return raster;
}

// [C] _rwResourcesPurge(), then the standard show-raster function (ourGlobals + 0x98) with (raster, a1, a2); the raster
// when that answered nonzero, else NULL.
// 0x001E79D8
extern "C" UnkStruct_RwRasterSub *RwRasterShowRaster(UnkStruct_RwRasterSub *raster, unsigned int dev, unsigned int flags)
{
    UnkStruct_ourGlobalsRaster *g = (UnkStruct_ourGlobalsRaster *)&ourGlobals;
    int r;

    _rwResourcesPurge();
    r = g->field_98(raster, (void *)dev, flags);
    if (r == 0) {
        return 0;
    }
    return raster;
}
