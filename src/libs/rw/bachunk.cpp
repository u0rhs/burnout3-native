// RenderWare 3.6 core: chunk groups (RW 3.1 file name: src/bachunkgroup.c; reading aid only).
// Written from the retail asm (decomp assembly/asm/rw/renderware.s). Tags: [C] confirmed from the asm, [L], [G].

extern unsigned char ourGlobals[];     // [C] 0x00670BD0, the RW globals block (0x4000 bytes, symbol table)
extern int chunkGroupModule;           // [C] gp-relative 0x004E2790 (8 bytes): +0 the globals' offset in ourGlobals
extern int D_004E2794;                 // [C] gp-relative 0x004E2794 = chunkGroupModule + 4: the module's open count
extern int _rwChunkGroupFListBlockSize;          // [C] gp-relative
extern int _rwChunkGroupFListPreallocBlocks;     // [C] gp-relative
extern unsigned char D_00670B80[];     // [C] in-place space for the chunk group free list

extern "C" void *RwFreeListCreateAndPreallocateSpace(int entrySize, int entriesPerBlock, int alignment,
                                                     int numBlocksToPreallocate, void *inPlaceSpace, unsigned int hint);
extern "C" int RwFreeListDestroy(void *freeList);

// [C] the chunk group globals: only the free list.
struct UnkStruct_chunkGroupModule {
    void *field_0;                     // [C] the free list (entries of 0x21, alignment 4, hint 0x40412)
};

// [C] chunkGroupModule = offset; +0 = RwFreeListCreateAndPreallocateSpace(0x21, _rwChunkGroupFListBlockSize, 4,
// _rwChunkGroupFListPreallocBlocks, D_00670B80, 0x40412); 0 -> returns 0; else open count += 1, returns the instance.
// 0x001DF168
extern "C" void *_rwChunkGroupOpen(void *instance, int offset, int size)
{
    void *freeList;

    chunkGroupModule = offset;
    freeList = RwFreeListCreateAndPreallocateSpace(0x21, _rwChunkGroupFListBlockSize, 4,
                                                   _rwChunkGroupFListPreallocBlocks, D_00670B80, 0x40412);
    ((UnkStruct_chunkGroupModule *)(ourGlobals + chunkGroupModule))->field_0 = freeList;
    if (freeList == 0) {
        return 0;
    }
    D_004E2794 = D_004E2794 + 1;
    return instance;
}

// [C] if the free list is set: RwFreeListDestroy it (not cleared); open count -= 1; returns the instance.
// 0x001DF1E0
extern "C" void *_rwChunkGroupClose(void *instance, int offset, int size)
{
    void *freeList = ((UnkStruct_chunkGroupModule *)(ourGlobals + chunkGroupModule))->field_0;

    if (freeList != 0) {
        RwFreeListDestroy(freeList);
    }
    D_004E2794 = D_004E2794 - 1;
    return instance;
}
