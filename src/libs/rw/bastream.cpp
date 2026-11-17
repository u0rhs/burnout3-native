// RenderWare 3.6 core: streams (RW 3.1 file name: src/bastream.c; reading aid only).
// Written from the retail asm (decomp assembly/asm/rw/renderware.s). Tags: [C] confirmed from the asm, [L], [G].

extern unsigned char ourGlobals[];     // [C] 0x00670BD0, the RW globals block (0x4000 bytes, symbol table)
extern int streamModule;               // [C] gp-relative 0x004E2778 (8 bytes): +0 the stream globals' offset in ourGlobals
extern int D_004E277C;                 // [C] gp-relative 0x004E277C = streamModule + 4: the module's open count
extern int _rwStreamFreeListBlockSize;           // [C] gp-relative
extern int _rwStreamFreeListPreallocBlocks;      // [C] gp-relative
extern unsigned char D_00670B30[];     // [C] in-place space for the stream free list

extern "C" void *RwFreeListCreateAndPreallocateSpace(int entrySize, int entriesPerBlock, int alignment,
                                                     int numBlocksToPreallocate, void *inPlaceSpace, unsigned int hint);
extern "C" int RwFreeListDestroy(void *freeList);

// [C] the stream globals ([L] rwStreamGlobals): only the free list.
struct UnkStruct_streamModule {
    void *field_0;                     // [C] the stream free list (entries of 0x24, alignment 4, hint 0x40404)
};

// [C] streamModule = offset; ourGlobals + offset +0 = RwFreeListCreateAndPreallocateSpace(0x24,
// _rwStreamFreeListBlockSize, 4, _rwStreamFreeListPreallocBlocks, D_00670B30, 0x40404); if that is 0 returns 0, else the
// open count (streamModule+4) += 1 and returns the instance.
// 0x001DD488
extern "C" void *_rwStreamModuleOpen(void *instance, int offset, int size)
{
    void *freeList;

    streamModule = offset;
    freeList = RwFreeListCreateAndPreallocateSpace(0x24, _rwStreamFreeListBlockSize, 4,
                                                   _rwStreamFreeListPreallocBlocks, D_00670B30, 0x40404);
    ((UnkStruct_streamModule *)(ourGlobals + streamModule))->field_0 = freeList;
    if (freeList == 0) {
        return 0;
    }
    D_004E277C = D_004E277C + 1;
    return instance;
}

// [C] if the free list is set: RwFreeListDestroy it (the pointer is not cleared); the open count -= 1; returns the
// instance.
// 0x001DD500
extern "C" void *_rwStreamModuleClose(void *instance, int offset, int size)
{
    void *freeList = ((UnkStruct_streamModule *)(ourGlobals + streamModule))->field_0;

    if (freeList != 0) {
        RwFreeListDestroy(freeList);
    }
    D_004E277C = D_004E277C - 1;
    return instance;
}
