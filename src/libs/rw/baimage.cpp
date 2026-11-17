// RenderWare 3.6 core: images (RW 3.1 file name: src/baimage.c; reading aid only).
// Written from the retail asm (decomp assembly/asm/rw/renderware.s). Tags: [C] confirmed from the asm, [L], [G].

extern unsigned char ourGlobals[];     // [C] 0x00670BD0, the RW globals block (0x4000 bytes, symbol table)
extern int imageModule;                // [C] gp-relative 0x004E27A8 (8 bytes): +0 the image globals' offset in ourGlobals
extern float D_004E06D4;               // [C] gp-relative literal 0.003921568859 (1 / 255)

// [C] the image globals at ourGlobals + imageModule (0x220 bytes: the 0x406 engine plugin's size [C CorePluginAttach]).
struct UnkStruct_imageModule {
    unsigned char pad_0[0xC];
    unsigned char field_C[0x100];      // [C] entry i = (int)(powf(i / 255, 1 / gamma) * 255 + 0.5) ([L] gammaTable)
    unsigned char field_10C[0x100];    // [C] entry i = (int)(powf(i / 255, gamma) * 255 + 0.5) ([L] invGammaTable)
    float field_20C;                   // [C] the gamma value ([L] gammaVal)
};

extern "C" float func_0011E388(float x, float y);   // [C] retail newlib powf (a tail jump to __ieee754_powf)

// [C] 1 / gamma first (div.s); then byte +0x10C = 0, the gamma at +0x20C, byte +0xC = 0; then for i = 1..255 (retail
// unrolls by 3), x = (float)i * D_004E06D4: field_C[i] = (int)(powf(x, 1 / gamma) * 255 + 0.5), then
// field_10C[i] = (int)(powf(x, gamma) * 255 + 0.5) (cvt.w.s, the low byte stored; imageModule re-read for each
// store). Returns 1.
// 0x001E68F8
extern "C" int RwImageSetGamma(float gamma)
{
    float invGamma = 1.0f / gamma;
    float x;
    float p;
    int i;

    ((UnkStruct_imageModule *)(ourGlobals + imageModule))->field_10C[0] = 0;
    ((UnkStruct_imageModule *)(ourGlobals + imageModule))->field_20C = gamma;
    ((UnkStruct_imageModule *)(ourGlobals + imageModule))->field_C[0] = 0;
    for (i = 1; i < 0x100; i++) {
        x = (float)i * D_004E06D4;
        p = func_0011E388(x, invGamma);
        ((UnkStruct_imageModule *)(ourGlobals + imageModule))->field_C[i] = (unsigned char)(int)(p * 255.0f + 0.5f);
        p = func_0011E388(x, gamma);
        ((UnkStruct_imageModule *)(ourGlobals + imageModule))->field_10C[i] = (unsigned char)(int)(p * 255.0f + 0.5f);
    }
    return 1;
}

extern int D_004E27AC;                 // [C] gp-relative 0x004E27AC = imageModule + 4: the module's open count
extern unsigned char imageTKList[];    // [C] 0x0049A290: the image plugin registry (0x18 bytes, symbol table)
extern int _rwImageFreeListBlockSize;            // [C] gp-relative
extern int _rwImageFreeListPreallocBlocks;       // [C] gp-relative
extern int _rwImageFormatFreeListPreallocBlocks; // [C] gp-relative 0x004E1794
extern unsigned char D_00674BF8[];     // [C] in-place space for the image free list
extern unsigned char D_00674C20[];     // [C] in-place space for the image format free list

extern "C" void *RwFreeListCreateAndPreallocateSpace(int entrySize, int entriesPerBlock, int alignment,
                                                     int numBlocksToPreallocate, void *inPlaceSpace, unsigned int hint);
extern "C" int RwFreeListDestroy(void *freeList);

// [C] a free-list entry of the image format list (0x34 bytes) as _rwImageClose drains it: the next entry at +0x30.
struct UnkStruct_imageFormatEntry {
    unsigned char pad_0[0x30];
    UnkStruct_imageFormatEntry *field_30;   // [C] next ([L] the registered image format chain)
};

// [C] the image globals as open / close use them (the gamma tables of RwImageSetGamma sit at +0xC / +0x10C / +0x20C).
struct UnkStruct_imageModuleOpen {
    void *field_0;                     // [C] the image free list (entries of imageTKList's first word, alignment 4)
    unsigned char *field_4;            // [C] a 0x100-byte block (rwmalloc, hint 0x1040406), first byte 0
    int field_8;                       // [C] 0x100
    unsigned char pad_C[0x204];
    unsigned char *field_210;          // [C] a 0x100-byte block (rwmalloc, hint 0x1040018)
    int field_214;                     // [C] 0x100 ([L] size of field_210)
    void *field_218;                   // [C] the image format free list (entries of 0x34, hint 0x40406)
    UnkStruct_imageFormatEntry *field_21C;   // [C] 0 here ([L] the format list head); close frees the chain
};

// [C] the RW allocator fields of ourGlobals.
struct UnkStruct_ourGlobalsImage {
    unsigned char pad_0[0x134];
    void *(*field_134)(unsigned int, unsigned int);  // [C] (size, hint) ([L] rwmalloc)
    void (*field_138)(void *);                       // [C] ([L] rwfree)
    unsigned char pad_13C[0x8];
    void *(*field_144)(void *, unsigned int);        // [C] (free list, hint)
    void *(*field_148)(void *, void *);              // [C] (free list, entry) ([L] memoryFree)
};

// [C] imageModule = offset; free list 1 = Create(first word of imageTKList, _rwImageFreeListBlockSize, 4,
// _rwImageFreeListPreallocBlocks, D_00674BF8, 0x40018) stored at +0 (0 -> return 0); free list 2 = Create(0x34,
// _rwImageFreeListBlockSize, 4, _rwImageFormatFreeListPreallocBlocks, D_00674C20, 0x40406) at +0x218 (0 -> list 1
// destroyed and cleared, return 0). Then +8 = 0x100 and +4 = rwmalloc(0x100, 0x1040406); if 0, list 2 and then list 1 are
// destroyed (+0x218 and +0 cleared) and 0 is returned (+8 stays 0x100). Else byte [+4] = 0, open count += 1,
// RwImageSetGamma(1.0f), +0x214 = 0x100, +0x21C = 0, +0x210 = rwmalloc(0x100, 0x1040018); if that is 0 the +4 block is
// freed, +4 = +8 = 0 and the same teardown of the two lists, return 0 (the count stays incremented). Else returns the
// instance.
// 0x001E46A0
extern "C" void *_rwImageOpen(void *instance, int offset, int size)
{
    UnkStruct_ourGlobalsImage *rw = (UnkStruct_ourGlobalsImage *)ourGlobals;
    UnkStruct_imageModuleOpen *g;
    void *freeList;
    unsigned char *block;

    imageModule = offset;
    freeList = RwFreeListCreateAndPreallocateSpace(*(int *)imageTKList, _rwImageFreeListBlockSize, 4,
                                                   _rwImageFreeListPreallocBlocks, D_00674BF8, 0x40018);
    g = (UnkStruct_imageModuleOpen *)(ourGlobals + imageModule);
    g->field_0 = freeList;
    if (freeList == 0) {
        return 0;
    }
    freeList = RwFreeListCreateAndPreallocateSpace(0x34, _rwImageFreeListBlockSize, 4,
                                                   _rwImageFormatFreeListPreallocBlocks, D_00674C20, 0x40406);
    g = (UnkStruct_imageModuleOpen *)(ourGlobals + imageModule);
    g->field_218 = freeList;
    if (freeList == 0) {
        goto destroyFirst;
    }
    g->field_8 = 0x100;
    block = (unsigned char *)rw->field_134(0x100, 0x1040406);
    g = (UnkStruct_imageModuleOpen *)(ourGlobals + imageModule);
    g->field_4 = block;
    if (block == 0) {
        goto destroySecond;
    }
    block[0] = 0;
    D_004E27AC = D_004E27AC + 1;
    RwImageSetGamma(1.0f);
    g = (UnkStruct_imageModuleOpen *)(ourGlobals + imageModule);
    g->field_214 = 0x100;
    g->field_21C = 0;
    block = (unsigned char *)rw->field_134(0x100, 0x1040018);
    g = (UnkStruct_imageModuleOpen *)(ourGlobals + imageModule);
    g->field_210 = block;
    if (block != 0) {
        return instance;
    }
    rw->field_138(g->field_4);
    g = (UnkStruct_imageModuleOpen *)(ourGlobals + imageModule);
    g->field_4 = 0;
    g->field_8 = 0;
destroySecond:
    RwFreeListDestroy(((UnkStruct_imageModuleOpen *)(ourGlobals + imageModule))->field_218);
    g = (UnkStruct_imageModuleOpen *)(ourGlobals + imageModule);
    g->field_218 = 0;
destroyFirst:
    RwFreeListDestroy(((UnkStruct_imageModuleOpen *)(ourGlobals + imageModule))->field_0);
    ((UnkStruct_imageModuleOpen *)(ourGlobals + imageModule))->field_0 = 0;
    return 0;
}

// [C] frees the +0x210 block when set (+0x214 and +0x210 cleared), the +4 block when set (+8, +4 cleared); then drains
// the chain at +0x21C: each entry is detached (+0x21C = entry->field_30) and returned to the format free list through
// ourGlobals+0x148; then the format list (+0x218) and the image list (+0) are destroyed when set and cleared; open
// count -= 1; returns the instance.
// 0x001E4840
extern "C" void *_rwImageClose(void *instance, int offset, int size)
{
    UnkStruct_ourGlobalsImage *rw = (UnkStruct_ourGlobalsImage *)ourGlobals;
    UnkStruct_imageModuleOpen *g = (UnkStruct_imageModuleOpen *)(ourGlobals + imageModule);
    UnkStruct_imageFormatEntry *entry;

    if (g->field_210 != 0) {
        rw->field_138(g->field_210);
        g = (UnkStruct_imageModuleOpen *)(ourGlobals + imageModule);
        g->field_214 = 0;
        g->field_210 = 0;
    }
    g = (UnkStruct_imageModuleOpen *)(ourGlobals + imageModule);
    if (g->field_4 != 0) {
        rw->field_138(g->field_4);
        g = (UnkStruct_imageModuleOpen *)(ourGlobals + imageModule);
        g->field_8 = 0;
        g->field_4 = 0;
    }
    while (((UnkStruct_imageModuleOpen *)(ourGlobals + imageModule))->field_21C != 0) {
        void *formatList;
        UnkStruct_imageFormatEntry *next;

        g = (UnkStruct_imageModuleOpen *)(ourGlobals + imageModule);
        entry = g->field_21C;
        formatList = g->field_218;
        next = entry->field_30;
        g->field_21C = next;
        rw->field_148(formatList, entry);
    }
    g = (UnkStruct_imageModuleOpen *)(ourGlobals + imageModule);
    if (g->field_218 != 0) {
        RwFreeListDestroy(g->field_218);
        g = (UnkStruct_imageModuleOpen *)(ourGlobals + imageModule);
        g->field_218 = 0;
    }
    g = (UnkStruct_imageModuleOpen *)(ourGlobals + imageModule);
    if (g->field_0 != 0) {
        RwFreeListDestroy(g->field_0);
        g = (UnkStruct_imageModuleOpen *)(ourGlobals + imageModule);
        g->field_0 = 0;
    }
    D_004E27AC = D_004E27AC - 1;
    return instance;
}
