// RenderWare 3.6 core: render pipelines and the pipeline heap (RW 3.1 file names: src/p2core.c, src/p2heap.c,
// src/p2renderstate.c; reading aid only).
// Written from the retail asm (decomp assembly/asm/rw/renderware.s). Tags: [C] confirmed from the asm, [L], [G].

// [C] the RW globals block (0x4000 bytes at 0x00670BD0), the fields these functions read.
struct UnkStruct_ourGlobals {
    unsigned char pad_0[0x134];
    void *(*field_134)(unsigned int, unsigned int);  // [C] (size, hint) ([L] rwmalloc)
    void (*field_138)(void *);                       // [C] ([L] rwfree)
    void *(*field_13C)(void *, unsigned int, unsigned int);   // [C] (pointer, new size, hint) ([L] rwrealloc)
    unsigned char pad_140[0x10];
    unsigned int field_150;                          // [C] compared with 3 ([L] engine status, started)
};
extern UnkStruct_ourGlobals ourGlobals;

extern int _rxPipelineGlobalsOffset;   // [C] gp-relative 0x004E28C0: the pipeline globals' offset in ourGlobals
extern int _rxHeapInitialSize;         // [C] gp-relative 0x004E1914
extern int _rxPipelineMaxNodes;        // [C] gp-relative 0x004E1918
extern int RxPipelineInstanced;        // [C] gp-relative 0x004E191C
extern void *_rxHeapGlobal;            // [C] gp-relative 0x004E1920: the global pipeline heap
extern int _rxPipesFreeListBlockSize;            // [C] gp-relative 0x004E1924
extern int _rxPipesFreeListPreallocBlocks;       // [C] gp-relative 0x004E1928
extern unsigned int D_004E1930;        // [C] 0x004E1930: a 4-byte constant used as the default value of two render state words
extern unsigned char D_00676418[];     // [C] in-place space for the pipelines free list

extern "C" void *RwFreeListCreateAndPreallocateSpace(int entrySize, int entriesPerBlock, int alignment,
                                                     int numBlocksToPreallocate, void *inPlaceSpace, unsigned int hint);
extern "C" int RwFreeListDestroy(void *freeList);
extern "C" int _rwerror(int code);

// [C] an error: {plugin ID 1, _rwerror(code)} ([L] RwError).
struct UnkStruct_RwErrorSet {
    int field_0;
    int field_4;
};
extern "C" UnkStruct_RwErrorSet *RwErrorSet(UnkStruct_RwErrorSet *code);

struct UnkStruct_rxHeapSuperBlock;

// [C] a heap, 0x1C bytes ([L] RxHeap).
struct UnkStruct_RxHeap {
    unsigned int field_0;              // [C] the (rounded) superblock size as asked ([L] initialSize)
    UnkStruct_rxHeapSuperBlock *field_4;    // [C] the first superblock ([L] head)
    unsigned char *field_8;            // [C] the first block of the first superblock ([L] firstBlock)
    void *field_C;                     // [C] array of 8-byte entries {size, block}, grown by HeapSuperBlockReset ([L] blockarray)
    unsigned int field_10;             // [C] entries allocated in field_C ([L] numBlocks)
    unsigned int field_14;             // [C] entries used
    int field_18;                      // [C] 1 while the heap is being built, 0 after ([L] dirty)
};

// [C] a superblock: header of 0x8B+ bytes in front of an aligned (0x80) data area ([L] RxHeapSuperBlockDescriptor).
struct UnkStruct_rxHeapSuperBlock {
    unsigned char *field_0;            // [C] the aligned data area start
    unsigned int field_4;              // [C] its size
    UnkStruct_rxHeapSuperBlock *field_8;    // [C] the next superblock
};

// [C] a heap block header, 0x20 bytes; the data area is delimited by a first and a last header ([L] rxHeapBlockHeader).
struct UnkStruct_rxHeapBlock {
    UnkStruct_rxHeapBlock *field_0;    // [C] previous block
    UnkStruct_rxHeapBlock *field_4;    // [C] next block
    unsigned int field_8;              // [C] size
    void *field_C;                     // [C] the entry in the heap's block array ([L] eEntry)
    unsigned int field_10;
    unsigned int field_14;
    unsigned int field_18;
    unsigned int field_1C;
};

// [C] one 8-byte entry of the block array ([L] RxHeapFreeBlock): size and block.
struct UnkStruct_rxHeapEntry {
    unsigned int field_0;
    UnkStruct_rxHeapBlock *field_4;
};

// [C] fills in the superblock sb (previous superblock prev, or 0) of heap h. If the entry array is full
// (h->field_14 >= h->field_10): h->field_10 += 0x20 and the array is reallocated with ourGlobals+0x13C (array, new
// count * 8, hint 0x1030409); on failure error {1, _rwerror(0x80000013)} is set, h->field_10 -= 0x20 and 0 is returned;
// on success every used entry's block gets its back pointer (+0xC) refreshed (entry i at array + 8i) and h->field_C =
// array. Then the next entry (index h->field_14, count += 1) describes the superblock's free block: at the area start
// the first header is zeroed (+0..+0xC) and copied as a whole (0x20 bytes) to the last header at area + size - 0x20; the
// free block header follows the first header (area + 0x20); first.next = free block, free block.prev = first,
// free block.next = last, last.prev = free block; free block size = size - 0x60; the entry = {size, free block}; the
// free block +0xC = entry. When prev is set, prev's last header's next = the new first header and the new first
// header's previous = prev's last header. Returns 1.
// 0x00210658
extern "C" int HeapSuperBlockReset(UnkStruct_rxHeapSuperBlock *sb, UnkStruct_rxHeapSuperBlock *prev, UnkStruct_RxHeap *h)
{
    unsigned int capacity = h->field_10;
    unsigned int used = h->field_14;
    UnkStruct_rxHeapEntry *array = (UnkStruct_rxHeapEntry *)h->field_C;
    UnkStruct_rxHeapEntry *entry;
    UnkStruct_rxHeapBlock *first;
    UnkStruct_rxHeapBlock *freeBlock;
    UnkStruct_rxHeapBlock *last;
    unsigned int copy[8];
    unsigned int size;
    unsigned int index;
    unsigned int i;

    if (!(used < capacity)) {
        UnkStruct_RwErrorSet err;

        capacity = capacity + 0x20;
        h->field_10 = capacity;
        array = (UnkStruct_rxHeapEntry *)ourGlobals.field_13C(array, capacity << 3, 0x1030409);
        if (array == 0) {
            err.field_0 = 1;
            err.field_4 = _rwerror(0x80000013);
            RwErrorSet(&err);
            h->field_10 = h->field_10 - 0x20;
        } else if (array != (UnkStruct_rxHeapEntry *)h->field_C) {
            for (i = 0; i < used; i++) {
                array[i].field_4->field_C = &array[i];
            }
            h->field_C = array;
        } else {
            h->field_C = array;
        }
    }
    if (array == 0) {
        return 0;
    }
    index = h->field_14;
    entry = array + index;
    h->field_14 = index + 1;
    if (entry == 0) {
        return 0;
    }
    first = (UnkStruct_rxHeapBlock *)sb->field_0;
    size = sb->field_4;
    first->field_4 = 0;
    freeBlock = (UnkStruct_rxHeapBlock *)((unsigned char *)first + 0x20);
    last = (UnkStruct_rxHeapBlock *)((unsigned char *)first + size - 0x20);
    first->field_0 = 0;
    first->field_8 = 0;
    first->field_C = 0;
    for (i = 0; i < 8; i++) {
        copy[i] = ((unsigned int *)first)[i];
    }
    for (i = 0; i < 8; i++) {
        ((unsigned int *)last)[i] = copy[i];
    }
    first->field_4 = freeBlock;
    freeBlock->field_0 = first;
    freeBlock->field_4 = last;
    last->field_0 = freeBlock;
    freeBlock->field_8 = size - 0x40 - 0x20;
    entry->field_0 = size - 0x40 - 0x20;
    entry->field_4 = freeBlock;
    freeBlock->field_C = entry;
    if (prev != 0) {
        UnkStruct_rxHeapBlock *prevLast = (UnkStruct_rxHeapBlock *)(prev->field_0 + prev->field_4 - 0x20);

        prevLast->field_4 = first;
        first->field_0 = prevLast;
    }
    return 1;
}

// [C] size = max(asked, 0x400); the heap descriptor = rwmalloc(0x1C, 0x40409), 0 -> return 0. size = (size + 0x1F) &
// ~0x1F, at least 0x80; the superblock = rwmalloc(size + 0x8B, 0x1040409) (the data area at (sb + 0x8B) & ~0x7F, +4 =
// size, +8 = 0); when that fails the descriptor is freed and 0 returned. Else field_4 = sb, field_0 = size,
// field_18 = 1, field_C = field_10 = field_14 = 0, HeapSuperBlockReset is run for every superblock of the chain (from
// sb->field_8, none here) and then for field_4; field_8 = the first superblock's area start when no previous one
// exists; field_18 = 0; returns the heap. On a failed reset the superblock and the descriptor are freed and 0 returned.
// 0x002113C8
extern "C" UnkStruct_RxHeap *RxHeapCreate(unsigned int size)
{
    UnkStruct_RxHeap *h;
    UnkStruct_rxHeapSuperBlock *sb;
    UnkStruct_rxHeapSuperBlock *prev;
    UnkStruct_rxHeapSuperBlock *cur;

    size = size > 0x3FF ? size : 0x400;
    h = (UnkStruct_RxHeap *)ourGlobals.field_134(0x1C, 0x40409);
    if (h == 0) {
        return 0;
    }
    size = (size + 0x1F) & 0xFFFFFFE0;
    if (!(size > 0x7F)) {
        size = 0x80;
    }
    sb = (UnkStruct_rxHeapSuperBlock *)ourGlobals.field_134(size + 0x8B, 0x1040409);
    if (sb != 0) {
        sb->field_4 = size;
        sb->field_8 = 0;
        sb->field_0 = (unsigned char *)(((unsigned long)sb + 0x8B) & ~0x7FUL);
        h->field_4 = sb;
        h->field_0 = size;
        prev = 0;
        cur = sb->field_8;
        h->field_18 = 1;
        h->field_C = 0;
        h->field_10 = 0;
        h->field_14 = 0;
        if (cur != 0) {
            do {
                if (HeapSuperBlockReset(cur, prev, h) == 0) {
                    goto fail;
                }
                if (prev == 0) {
                    h->field_8 = cur->field_0;
                }
                prev = cur;
                cur = cur->field_8;
            } while (cur != 0);
        }
        cur = h->field_4;
        if (HeapSuperBlockReset(cur, prev, h) != 0) {
            if (prev == 0) {
                h->field_8 = cur->field_0;
            }
            h->field_18 = 0;
            return h;
        }
fail:
        ourGlobals.field_138(sb);
    }
    ourGlobals.field_138(h);
    return 0;
}

// [C] when the heap is set: frees its block array (field_C, when set, cleared), then every superblock of the chain
// from field_4 (each next pointer read first), then the descriptor itself.
// 0x00211560
extern "C" void RxHeapDestroy(UnkStruct_RxHeap *h)
{
    UnkStruct_rxHeapSuperBlock *sb;
    UnkStruct_rxHeapSuperBlock *next;

    if (h == 0) {
        return;
    }
    if (h->field_C != 0) {
        ourGlobals.field_138(h->field_C);
        h->field_C = 0;
    }
    sb = h->field_4;
    if (sb != 0) {
        do {
            next = sb->field_8;
            if (sb != 0) {
                ourGlobals.field_138(sb);
            }
            sb = next;
        } while (sb != 0);
    }
    ourGlobals.field_138(h);
}

// [C] a render state vector: 0x2C bytes of 11 words ([L] RxRenderStateVector).
struct UnkStruct_RxRenderStateVector {
    unsigned int field_0;              // [C] 7 in the defaults
    unsigned int field_4;              // [C] 2
    unsigned int field_8;              // [C] 5
    unsigned int field_C;              // [C] 6
    unsigned int field_10;             // [C] 0
    unsigned int field_14;             // [C] 1
    unsigned int field_18;             // [C] 1
    unsigned int field_1C;             // [C] 2
    unsigned int field_20;             // [C] D_004E1930
    unsigned int field_24;             // [C] 0
    unsigned int field_28;             // [C] D_004E1930
};

// [C] the pipeline globals at ourGlobals + _rxPipelineGlobalsOffset ([L] rxPipelineGlobals).
struct UnkStruct_rxPipelineGlobals {
    void *field_0;                     // [C] the pipelines free list (entries of 0x34, alignment 4, hint 0x40409)
    UnkStruct_RxRenderStateVector field_4;   // [C] the default render state vector, +4..+0x2F
    unsigned int field_30;             // [C] 0
    unsigned int field_34;             // [C] 0
    int field_38;                      // [C] _rxPipelineMaxNodes
};

// [C] with a NULL vector: error {1, _rwerror(0x80000016)}, returns 0. When ourGlobals+0x150 == 3: copies the 0x2C
// bytes at the pipeline globals + 4 to the vector and returns it. Otherwise the vector must be the pipeline globals' own
// +4 (else error {1, _rwerror(0x80000018)}, returns 0) and it is filled with the defaults: 7, 2, 5, 6, 0, 1, 1, 2,
// D_004E1930, 0, D_004E1930 (words 0..10). Returns the vector.
// 0x002116D0
extern "C" UnkStruct_RxRenderStateVector *RxRenderStateVectorSetDefaultRenderStateVector(UnkStruct_RxRenderStateVector *vec)
{
    UnkStruct_RwErrorSet err;
    UnkStruct_rxPipelineGlobals *g;
    unsigned int constant;

    if (vec == 0) {
        err.field_0 = 1;
        err.field_4 = _rwerror(0x80000016);
        RwErrorSet(&err);
        return 0;
    }
    g = (UnkStruct_rxPipelineGlobals *)((unsigned char *)&ourGlobals + _rxPipelineGlobalsOffset);
    if (ourGlobals.field_150 == 3) {
        vec->field_0 = g->field_4.field_0;
        vec->field_4 = g->field_4.field_4;
        vec->field_8 = g->field_4.field_8;
        vec->field_C = g->field_4.field_C;
        vec->field_10 = g->field_4.field_10;
        vec->field_14 = g->field_4.field_14;
        vec->field_18 = g->field_4.field_18;
        vec->field_1C = g->field_4.field_1C;
        vec->field_20 = g->field_4.field_20;
        vec->field_24 = g->field_4.field_24;
        vec->field_28 = g->field_4.field_28;
        return vec;
    }
    if (vec != &g->field_4) {
        err.field_0 = 1;
        err.field_4 = _rwerror(0x80000018);
        RwErrorSet(&err);
        return 0;
    }
    constant = D_004E1930;
    vec->field_0 = 7;
    vec->field_8 = 5;
    vec->field_C = 6;
    vec->field_18 = 1;
    vec->field_1C = 2;
    vec->field_4 = 2;
    vec->field_10 = 0;
    vec->field_14 = 1;
    vec->field_20 = constant;
    vec->field_24 = 0;
    vec->field_28 = constant;
    return vec;
}

// [C] when RxPipelineInstanced is 0: _rxHeapGlobal = RxHeapCreate(_rxHeapInitialSize); if that is set: the pipelines
// free list = Create(0x34, _rxPipesFreeListBlockSize, 4, _rxPipesFreeListPreallocBlocks, D_00676418, 0x40409) stored
// at the pipeline globals +0; on 0 the heap is destroyed, _rxHeapGlobal = 0 and 0 returned. Else globals +0x38 =
// _rxPipelineMaxNodes, RxRenderStateVectorSetDefaultRenderStateVector(globals + 4), RxPipelineInstanced = 1, globals
// +0x30 = +0x34 = 0; returns 1. Returns 0 when already instanced or when the heap failed.
// 0x0020F7E8
extern "C" int _rxPipelineOpen(void)
{
    UnkStruct_rxPipelineGlobals *g;
    void *freeList;

    if (RxPipelineInstanced != 0) {
        return 0;
    }
    _rxHeapGlobal = RxHeapCreate(_rxHeapInitialSize);
    if (_rxHeapGlobal == 0) {
        return 0;
    }
    freeList = RwFreeListCreateAndPreallocateSpace(0x34, _rxPipesFreeListBlockSize, 4, _rxPipesFreeListPreallocBlocks,
                                                   D_00676418, 0x40409);
    g = (UnkStruct_rxPipelineGlobals *)((unsigned char *)&ourGlobals + _rxPipelineGlobalsOffset);
    g->field_0 = freeList;
    if (freeList == 0) {
        RxHeapDestroy((UnkStruct_RxHeap *)_rxHeapGlobal);
        _rxHeapGlobal = 0;
        return 0;
    }
    g->field_38 = _rxPipelineMaxNodes;
    RxRenderStateVectorSetDefaultRenderStateVector(&g->field_4);
    RxPipelineInstanced = 1;
    g = (UnkStruct_rxPipelineGlobals *)((unsigned char *)&ourGlobals + _rxPipelineGlobalsOffset);
    g->field_30 = 0;
    g->field_34 = 0;
    return 1;
}

// [C] when RxPipelineInstanced is set: RwFreeListDestroy(globals +0), globals +0 = 0, RxHeapDestroy(_rxHeapGlobal),
// _rxHeapGlobal = 0, RxPipelineInstanced = 0. Returns 1.
// 0x0020FE00
extern "C" int _rxPipelineClose(void)
{
    UnkStruct_rxPipelineGlobals *g;

    if (RxPipelineInstanced != 0) {
        g = (UnkStruct_rxPipelineGlobals *)((unsigned char *)&ourGlobals + _rxPipelineGlobalsOffset);
        RwFreeListDestroy(g->field_0);
        g = (UnkStruct_rxPipelineGlobals *)((unsigned char *)&ourGlobals + _rxPipelineGlobalsOffset);
        g->field_0 = 0;
        RxHeapDestroy((UnkStruct_RxHeap *)_rxHeapGlobal);
        _rxHeapGlobal = 0;
        RxPipelineInstanced = 0;
    }
    return 1;
}

// [C] _rxPipelineGlobalsOffset = offset; the instance when _rxPipelineOpen() gives non-zero, else 0.
// 0x0020F040
extern "C" void *_rwRenderPipelineOpen(void *instance, int offset, int size)
{
    _rxPipelineGlobalsOffset = offset;
    if (_rxPipelineOpen() == 0) {
        instance = 0;
    }
    return instance;
}

// [C] _rxPipelineClose(); returns the instance.
// 0x0020F070
extern "C" void *_rwRenderPipelineClose(void *instance, int offset, int size)
{
    _rxPipelineClose();
    return instance;
}

// ---------------------------------------------------------------------------------------------------------------
// boot2 additions (track rw): the pipeline's per-camera hook.

// [C] an empty function (jr $ra; nop): the render pipeline needs nothing per camera at begin update in this build
// (CameraBeginUpdate calls it with the camera).
// 0x0020F0A0
extern "C" void _rwPipeInitForCamera(void *camera)
{
}
