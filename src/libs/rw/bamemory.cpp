// RenderWare 3.6 core: memory and free lists (RW 3.1 file name: src/bamemory.c; reading aid only).
// Written from the retail asm (decomp assembly/asm/rw/renderware.s). Tags: [C] confirmed from the asm, [L], [G].

// [L] RwLLLink / RwLinkList (one link: next at +0, prev at +4); an empty list points at itself [C _rwMemoryOpen].
struct UnkStruct_freeListList {
    UnkStruct_freeListList *field_0;   // [L] next
    UnkStruct_freeListList *field_4;   // [L] prev
};

// [C] 0x24 bytes (FreeListCreate allocates 0x24 when there is no meta free list) [L] RwFreeList; same fields as the
// 3.5 header's release layout.
struct UnkStruct_FreeListCreate {
    unsigned int field_0;               // [C] entry size rounded up to field_C ([L] entrySize)
    unsigned int field_4;               // [C] entries per block ([L] entriesPerBlock)
    unsigned int field_8;               // [C] (entriesPerBlock + 7) >> 3: bytes of a block's entry bitmap ([L] heapSize)
    unsigned int field_C;               // [C] alignment, 0x10 when 0 is passed ([L] alignment)
    UnkStruct_freeListList field_10;    // [C] the list of allocated blocks ([L] blockList)
    unsigned int field_18;              // [C] 2 when allocated, 3 when in place ([L] flags; bit 0 static)
    UnkStruct_freeListList field_1C;    // [C] link in _freeListList ([L] link)
};

// [C] 16 bytes copied from RwEngineInit's memFuncs to ourGlobals + 0x134 ([L] RwMemoryFunctions; 3.6 passes a hint
// as the last argument of the allocating ones: FreeListCreate calls field_0 with (size, hint)).
struct UnkStruct_RwOsGetMemoryInterface {
    void *(*field_0)(unsigned int, unsigned int);                // [C] HMalloc by default ([L] rwmalloc)
    void (*field_4)(void *);                                     // [C] free by default ([L] rwfree)
    void *(*field_8)(void *, unsigned int, unsigned int);        // [C] HRealloc by default ([L] rwrealloc)
    void *(*field_C)(unsigned int, unsigned int, unsigned int);  // [C] HCalloc by default ([L] rwcalloc)
};

struct UnkStruct_ourGlobals {
    unsigned char pad_0[0x134];
    UnkStruct_RwOsGetMemoryInterface field_134;   // [L] memoryFuncs
    void *(*field_144)(void *, unsigned int);     // [C] called with (meta free list, hint & 0xFF0000) ([L] memoryAlloc)
    void *(*field_148)(void *, void *);           // [C] called with (meta free list, free-list struct) ([L] memoryFree)
};
extern UnkStruct_ourGlobals ourGlobals;
extern int FreeListsEnabled;                       // [C] gp-relative word written by _rwFreeListEnable
extern int _freeListModuleOpen;                    // [C] gp-relative
extern UnkStruct_freeListList _freeListList;       // [C] gp-relative 8 bytes: every free list's field_1C link
extern UnkStruct_FreeListCreate *D_004E172C;       // [C] gp-relative ([L] the free list of free-list structs)
extern unsigned char D_00670B08[];                 // [C] in-place space for D_004E172C's own struct (0x24 bytes)

extern "C" void *malloc(unsigned int);
extern "C" void *realloc(void *, unsigned int);
extern "C" void *calloc(unsigned int, unsigned int);
extern "C" void *memset(void *, int, unsigned int);
extern "C" void *memcpy(void *, const void *, unsigned int);
extern "C" void *HMalloc(unsigned int size);
extern "C" void *HRealloc(void *p, unsigned int size);
extern "C" void *HCalloc(unsigned int n, unsigned int size);
// [C] free (0x00123330): spelled func_00123330 so decomp2host binds retail's newlib free rather than the host's (only
// its address is stored).
extern "C" void func_00123330(void *);

// [C] every block of field_10 is unlinked and given to the free function (ourGlobals + 0x138); then, unless field_18
// bit 0 is set ([L] rwFREELISTFLAG_STATIC: the struct was in place), the struct itself goes back: through the free
// function when it is the meta free list D_004E172C itself or there is none, else through ourGlobals + 0x148 with
// (D_004E172C, freeList). No defined result ([C] v0 is whatever the last call left).
// 0x001DBF60
extern "C" void _RwFreeListFree(UnkStruct_FreeListCreate *freeList)
{
    UnkStruct_freeListList *block;

    while (freeList->field_10.field_0 != &freeList->field_10) {
        block = freeList->field_10.field_0;
        block->field_4->field_0 = block->field_0;
        block->field_0->field_4 = block->field_4;
        ourGlobals.field_134.field_4(block);
    }
    if ((freeList->field_18 & 1) == 0) {
        if (D_004E172C == freeList || D_004E172C == 0) {
            ourGlobals.field_134.field_4(freeList);
        } else {
            ourGlobals.field_148(D_004E172C, freeList);
        }
    }
}

// [C] unlinks the list from _freeListList (field_1C), then the same body as _RwFreeListFree (inlined in retail);
// returns 1.
// 0x001DB7A8
extern "C" int RwFreeListDestroy(UnkStruct_FreeListCreate *freeList)
{
    UnkStruct_freeListList *block;

    freeList->field_1C.field_4->field_0 = freeList->field_1C.field_0;
    freeList->field_1C.field_0->field_4 = freeList->field_1C.field_4;
    while (freeList->field_10.field_0 != &freeList->field_10) {
        block = freeList->field_10.field_0;
        block->field_4->field_0 = block->field_0;
        block->field_0->field_4 = block->field_4;
        ourGlobals.field_134.field_4(block);
    }
    if ((freeList->field_18 & 1) == 0) {
        if (D_004E172C == freeList || D_004E172C == 0) {
            ourGlobals.field_134.field_4(freeList);
        } else {
            ourGlobals.field_148(D_004E172C, freeList);
        }
    }
    return 1;
}

// [C] FreeListCreate and RwFreeListCreateAndPreallocateSpace are the same code at two addresses (instruction for
// instruction, branch targets aside). numBlocksToPreallocate counts only while free lists are enabled; each
// preallocated block is (bitmap + entries + 8-byte link + alignment) - 1 bytes from the memory functions with the full
// hint, linked at the head of field_10, its bitmap cleared after linking. A failed block allocation frees the list
// with _RwFreeListFree and returns NULL. The new list is linked at the head of _freeListList.
// 0x001DC020
extern "C" UnkStruct_FreeListCreate *FreeListCreate(int entrySize, int entriesPerBlock, int alignment,
                                                    int numBlocksToPreallocate, void *inPlaceSpace, unsigned int hint)
{
    UnkStruct_FreeListCreate *freeList;
    UnkStruct_freeListList *block;
    unsigned int numBlocks = 0;
    unsigned int align = 0x10;
    unsigned int entry;
    unsigned int bitmapSize;
    unsigned int blockSize;

    if (FreeListsEnabled != 0) {
        numBlocks = numBlocksToPreallocate;
    }
    if (alignment != 0) {
        align = alignment;
    }
    freeList = (UnkStruct_FreeListCreate *)inPlaceSpace;
    if (freeList == 0) {
        if (D_004E172C != 0) {
            freeList = (UnkStruct_FreeListCreate *)ourGlobals.field_144(D_004E172C, hint & 0xFF0000);
        } else {
            freeList = (UnkStruct_FreeListCreate *)ourGlobals.field_134.field_0(0x24, hint & 0xFF0000);
        }
        if (freeList == 0) {
            return 0;
        }
        freeList->field_18 = 2;
    } else {
        freeList->field_18 = 3;
    }
    entry = ((unsigned int)entrySize - 1 + align) & -align;
    bitmapSize = ((unsigned int)entriesPerBlock + 7) >> 3;
    freeList->field_0 = entry;
    freeList->field_4 = entriesPerBlock;
    freeList->field_C = align;
    freeList->field_8 = bitmapSize;
    freeList->field_10.field_0 = &freeList->field_10;
    freeList->field_10.field_4 = &freeList->field_10;
    if (numBlocks != 0) {
        blockSize = bitmapSize + (entry * entriesPerBlock + 8) + align;
        do {
            block = (UnkStruct_freeListList *)ourGlobals.field_134.field_0(blockSize - 1, hint);
            if (block == 0) {
                _RwFreeListFree(freeList);
                return 0;
            }
            block->field_4 = 0;
            block->field_0 = 0;
            numBlocks--;
            block->field_4 = &freeList->field_10;
            block->field_0 = freeList->field_10.field_0;
            freeList->field_10.field_0->field_4 = block;
            freeList->field_10.field_0 = block;
            memset((unsigned char *)block + 8, 0, bitmapSize);
        } while (numBlocks != 0);
    }
    freeList->field_1C.field_4 = &_freeListList;
    freeList->field_1C.field_0 = _freeListList.field_0;
    _freeListList.field_0->field_4 = &freeList->field_1C;
    _freeListList.field_0 = &freeList->field_1C;
    return freeList;
}

// [C] the same code as FreeListCreate (above), exported.
// 0x001DAFC8
extern "C" UnkStruct_FreeListCreate *RwFreeListCreateAndPreallocateSpace(int entrySize, int entriesPerBlock,
                                                                         int alignment, int numBlocksToPreallocate,
                                                                         void *inPlaceSpace, unsigned int hint)
{
    UnkStruct_FreeListCreate *freeList;
    UnkStruct_freeListList *block;
    unsigned int numBlocks = 0;
    unsigned int align = 0x10;
    unsigned int entry;
    unsigned int bitmapSize;
    unsigned int blockSize;

    if (FreeListsEnabled != 0) {
        numBlocks = numBlocksToPreallocate;
    }
    if (alignment != 0) {
        align = alignment;
    }
    freeList = (UnkStruct_FreeListCreate *)inPlaceSpace;
    if (freeList == 0) {
        if (D_004E172C != 0) {
            freeList = (UnkStruct_FreeListCreate *)ourGlobals.field_144(D_004E172C, hint & 0xFF0000);
        } else {
            freeList = (UnkStruct_FreeListCreate *)ourGlobals.field_134.field_0(0x24, hint & 0xFF0000);
        }
        if (freeList == 0) {
            return 0;
        }
        freeList->field_18 = 2;
    } else {
        freeList->field_18 = 3;
    }
    entry = ((unsigned int)entrySize - 1 + align) & -align;
    bitmapSize = ((unsigned int)entriesPerBlock + 7) >> 3;
    freeList->field_0 = entry;
    freeList->field_4 = entriesPerBlock;
    freeList->field_C = align;
    freeList->field_8 = bitmapSize;
    freeList->field_10.field_0 = &freeList->field_10;
    freeList->field_10.field_4 = &freeList->field_10;
    if (numBlocks != 0) {
        blockSize = bitmapSize + (entry * entriesPerBlock + 8) + align;
        do {
            block = (UnkStruct_freeListList *)ourGlobals.field_134.field_0(blockSize - 1, hint);
            if (block == 0) {
                _RwFreeListFree(freeList);
                return 0;
            }
            block->field_4 = 0;
            block->field_0 = 0;
            numBlocks--;
            block->field_4 = &freeList->field_10;
            block->field_0 = freeList->field_10.field_0;
            freeList->field_10.field_0->field_4 = block;
            freeList->field_10.field_0 = block;
            memset((unsigned char *)block + 8, 0, bitmapSize);
        } while (numBlocks != 0);
    }
    freeList->field_1C.field_4 = &_freeListList;
    freeList->field_1C.field_0 = _freeListList.field_0;
    _freeListList.field_0->field_4 = &freeList->field_1C;
    _freeListList.field_0 = &freeList->field_1C;
    return freeList;
}

// [C] returns &ourGlobals + 0x134.
// 0x001DB798
extern "C" UnkStruct_RwOsGetMemoryInterface *RwOsGetMemoryInterface(void)
{
    return &ourGlobals.field_134;
}

// [C] destroys every free list still linked in _freeListList (each link is its list + 0x1C), then the meta free list
// D_004E172C (not in the list: _rwMemoryOpen unlinked it).
// 0x001DBE08
extern "C" void _rwMemoryClose(void)
{
    while (_freeListList.field_0 != &_freeListList) {
        RwFreeListDestroy((UnkStruct_FreeListCreate *)((unsigned char *)(UnkStruct_freeListList *)_freeListList.field_0 - 0x1C));
    }
    RwFreeListDestroy(D_004E172C);
    D_004E172C = 0;
    _freeListModuleOpen = 0;
}

// [C] the meta free list holds 0x24-byte entries (free-list structs), 0x10 per block, aligned 0x10, nothing
// preallocated, its own struct in place at D_00670B08, hint 0x40000; it is unlinked from _freeListList right after
// creation. Then the memory functions: a copy of memFuncs (16 bytes, unaligned loads) or the defaults HMalloc / free /
// HRealloc / HCalloc. Returns 0 only when the meta free list could not be made (before the memory functions are set).
// 0x001DBE58
extern "C" int _rwMemoryOpen(const UnkStruct_RwOsGetMemoryInterface *memFuncs)
{
    int ok;

    _freeListList.field_4 = &_freeListList;
    _freeListModuleOpen = 1;
    _freeListList.field_0 = &_freeListList;
    D_004E172C = FreeListCreate(0x24, 0x10, 0x10, 0, D_00670B08, 0x40000);
    if (D_004E172C == 0) {
        _freeListModuleOpen = 0;
        ok = 0;
    } else {
        ok = 1;
        D_004E172C->field_1C.field_4->field_0 = D_004E172C->field_1C.field_0;
        D_004E172C->field_1C.field_0->field_4 = D_004E172C->field_1C.field_4;
    }
    if (ok == 0) {
        return 0;
    }
    if (memFuncs != 0) {
        ourGlobals.field_134 = *memFuncs;
        return 1;
    }
    ourGlobals.field_134.field_C = HCalloc;
    ourGlobals.field_134.field_0 = HMalloc;
    ourGlobals.field_134.field_4 = func_00123330;
    ourGlobals.field_134.field_8 = HRealloc;
    return 1;
}

// 0x001DBF58
extern "C" void _rwFreeListEnable(int enable)
{
    FreeListsEnabled = enable;   // [C] sw a0, %gp_rel(FreeListsEnabled)
}

// The default memory functions (RwMemoryFunctions: rwmalloc / rwfree / rwrealloc / rwcalloc take a trailing hint
// argument in 3.6 [L 3.5 headers]); each is a tail of the newlib call with a0 (a1) unchanged [C asm: jal malloc].
// 0x001DC390
extern "C" void *HMalloc(unsigned int size)
{
    return malloc(size);
}

// 0x001DC3B0
extern "C" void *HRealloc(void *p, unsigned int size)
{
    return realloc(p, size);
}

// 0x001DC3D0
extern "C" void *HCalloc(unsigned int n, unsigned int size)
{
    return calloc(n, size);
}

// [C] first fit over the list's blocks (field_10, from its head): in each block the bitmap bytes (field_8 of them,
// at block + 8) are scanned in order with a countdown of field_4 entries; a byte 0xFF only takes 8 off the count;
// otherwise its bits from 0x80 down are tried while the count lasts (the count drops by one per set bit, even past
// the bitmap's last valid entry; retail unrolls that by 4 on the count). The first clear bit is set (the byte re-read,
// ORed, stored) and the entry returned: ((block + field_8 + 8 + field_C - 1) & -field_C) + index * field_0, with
// index = byte * 8 + bit. With no free entry a new block of field_8 + field_4 * field_0 + 8 + field_C - 1 bytes comes
// from the memory functions (ourGlobals + 0x134, with the hint); NULL when that fails. Its bitmap is cleared, it is
// linked at the head of field_10 (block + 4, block + 0, old head + 4, field_10, in that order), its first bit set
// (byte 0x80) and its first entry returned.
// 0x001DB338
extern "C" void *_rwFreeListAllocReal(UnkStruct_FreeListCreate *freeList, unsigned int hint)
{
    UnkStruct_freeListList *block;
    unsigned char *bitmap;
    unsigned int heapSize = freeList->field_8;
    unsigned int byteIndex;
    unsigned int bits;
    unsigned int mask;
    unsigned int bit;
    unsigned int base;
    int remaining;
    unsigned int result = 0;

    block = freeList->field_10.field_0;
    if (block != &freeList->field_10) {
        do {
            remaining = freeList->field_4;
            bitmap = (unsigned char *)block + 8;
            base = 0;
            for (byteIndex = 0; byteIndex < heapSize; byteIndex++) {
                bits = *bitmap;
                if (bits == 0xFF) {
                    remaining -= 8;
                } else if (remaining != 0) {
                    bit = 0;
                    for (;;) {
                        mask = (0x80 >> bit) & 0xFF;
                        if ((bits & mask) == 0) {
                            *bitmap = (unsigned char)(mask | *bitmap);
                            result = (((unsigned int)block + heapSize + 8 + freeList->field_C - 1) & -freeList->field_C)
                                     + (base + bit) * freeList->field_0;
                            break;
                        }
                        bit++;
                        remaining--;
                        if (bit >= 8 || remaining == 0) {
                            break;
                        }
                    }
                }
                if (result != 0) {
                    break;
                }
                base += 8;
                bitmap++;
            }
            block = block->field_0;
        } while (block != &freeList->field_10 && result == 0);
    }
    if (result != 0) {
        return (void *)result;
    }
    block = (UnkStruct_freeListList *)ourGlobals.field_134.field_0(
        heapSize + (freeList->field_4 * freeList->field_0 + 8) + freeList->field_C - 1, hint);
    if (block == 0) {
        return 0;
    }
    memset((unsigned char *)block + 8, 0, heapSize);
    block->field_4 = &freeList->field_10;
    block->field_0 = freeList->field_10.field_0;
    freeList->field_10.field_0->field_4 = block;
    freeList->field_10.field_0 = block;
    *((unsigned char *)block + 8) = 0x80;
    return (void *)(((unsigned int)block + heapSize + 8 + freeList->field_C - 1) & -freeList->field_C);
}

typedef void (*UnkFn_001DBBB8)(void *, void *);   // [C] (entry, data)

// [C] for each block of field_10 (from its head; the next block read before the callbacks run): a copy of its bitmap
// (field_8 bytes, read once at entry) from the memory functions (ourGlobals + 0x134, hint 0x10000; NULL returned when
// that fails), memcpy'd; every set bit of every nonzero copied byte, 0x80 first (all 8 bits, not bounded by field_4;
// retail unrolls by 4), calls fpCallBack(entry, data), entry = ((block + field_8 + 8 + field_C - 1) & -field_C) +
// index * field_0 (field_0 / field_C re-read per call); then the copy is freed (ourGlobals + 0x138). Returns
// freeList.
// 0x001DBBB8
extern "C" UnkStruct_FreeListCreate *RwFreeListForAllUsed(UnkStruct_FreeListCreate *freeList,
                                                          UnkFn_001DBBB8 fpCallBack, void *data)
{
    unsigned int heapSize = freeList->field_8;
    UnkStruct_freeListList *block;
    UnkStruct_freeListList *next;
    unsigned char *copy;
    unsigned int byteIndex;
    unsigned int bits;
    unsigned int bit;
    unsigned int entry;

    block = freeList->field_10.field_0;
    while (block != &freeList->field_10) {
        copy = (unsigned char *)ourGlobals.field_134.field_0(heapSize, 0x10000);
        if (copy == 0) {
            return 0;
        }
        memcpy(copy, (unsigned char *)block + 8, heapSize);
        next = block->field_0;
        for (byteIndex = 0; byteIndex < heapSize; byteIndex++) {
            bits = copy[byteIndex];
            if (bits == 0) {
                continue;
            }
            for (bit = 0; bit < 8; bit++) {
                if ((bits & ((0x80 >> bit) & 0xFF)) != 0) {
                    entry = (((unsigned int)block + heapSize + 8 + freeList->field_C - 1) & -freeList->field_C)
                            + (byteIndex * 8 + bit) * freeList->field_0;
                    fpCallBack((void *)entry, data);
                }
            }
        }
        ourGlobals.field_134.field_4(copy);
        block = next;
    }
    return freeList;
}

// ---------------------------------------------------------------------------------------------------------------
// boot2 additions (track rw): _rwFreeListFreeReal.

// [C] a block of a free list: +0 next, +4 prev (links in field_10's list), +8 the entry bitmap (field_8 bytes), then
// the entries (at block + 8 + bitmap size).
struct UnkStruct_freeListBlock {
    UnkStruct_freeListBlock *field_0;  // [C] next
    UnkStruct_freeListBlock *field_4;  // [C] previous
    unsigned char field_8[1];          // [C] the bitmap
};

// [C] finds the block of freeList (field_10's list) whose entry range [block + 8 + bitmap size, + entries * entrySize]
// holds ptr (the end is inclusive), clears the entry's bit (index = (ptr - first) / entrySize; byte index >> 3, mask
// 0x80 >> (index & 7)). If the list's flags bit 1 is set ([L] rwFREELISTFLAG_FREEBLOCKS) and the block's bitmap is all
// zero (byte sum 0), the block is unlinked and given to the free function (ourGlobals + 0x138). Returns the list when ptr
// was found (even if no block was freed), else NULL.
// 0x001DB8A8
extern "C" UnkStruct_FreeListCreate *_rwFreeListFreeReal(UnkStruct_FreeListCreate *freeList, void *ptr)
{
    UnkStruct_freeListList *head = &freeList->field_10;
    UnkStruct_freeListBlock *block;
    unsigned int bitmapSize;
    unsigned char *first;
    unsigned int index;
    unsigned char *byte;
    unsigned int sum;
    unsigned int i;

    block = (UnkStruct_freeListBlock *)freeList->field_10.field_0;
    if ((UnkStruct_freeListList *)block == head) {
        return 0;
    }
    bitmapSize = freeList->field_8;
    first = (unsigned char *)block + bitmapSize + 8;
    for (;;) {
        if ((unsigned char *)ptr < first) {
            block = block->field_0;
        } else if (first + freeList->field_4 * freeList->field_0 < (unsigned char *)ptr) {
            block = block->field_0;
        } else {
            index = (unsigned int)((unsigned char *)ptr - first) / freeList->field_0;
            byte = block->field_8 + (index >> 3);
            *byte = (unsigned char)(*byte & ~(0x80 >> (index & 7)));
            if ((freeList->field_18 & 2) == 0) {
                return freeList;
            }
            sum = 0;
            if (bitmapSize != 0) {
                for (i = 0; i < bitmapSize; i++) {
                    sum = sum + block->field_8[i];
                }
            }
            if (sum == 0) {
                block->field_4->field_0 = block->field_0;
                block->field_0->field_4 = block->field_4;
                ourGlobals.field_134.field_4(block);
            }
            return freeList;
        }
        if ((UnkStruct_freeListList *)block == head) {
            break;
        }
        first = (unsigned char *)block + bitmapSize + 8;
    }
    return 0;
}
