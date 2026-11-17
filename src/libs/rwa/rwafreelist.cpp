// rwafreelist.o (librwacore.a) -- RWA's free lists: fixed-size entries in blocks with a used-bit map, the same scheme
// as RenderWare's own free lists. Retail 0x298DA0-0x299550 [C E map].
//
// Every list is linked through `link` into _freeListList (D_004E2B80); the free-list structs themselves come from the
// master list (_masterFreeListPtr, a static struct D_01E7D2C0) unless the caller provides one.
#include "rwa/rwacore.h"

extern RwUInt32 D_004E2B78;     // _freeListModuleOpen [L name, research/retail_data_names.csv]
extern RwaFreeList *_masterFreeListPtr;
extern RwLinkList D_004E2B80;    // _freeListList: the sentinel of all free lists
extern RwaFreeList D_01E7D2C0;  // the master list's own struct
extern "C" void *memset(void *dst, int c, unsigned int n);
extern "C" void RwaFreeListFree(RwaFreeList *fl, void *entry);

// The first entry of a block: past the link and the bit map, rounded up to the alignment [C 0x298FC4-0x298FE0].
static char *func_00298F00_inl1(RwaFreeList *fl, UnkStruct_00298F00 *block)
{
    unsigned int a = fl->alignment;
    return (char *)(((unsigned int)((char *)block + fl->heapSize) + a + 7) & ~(a - 1));
}

// Links `block` as the list's first block.
static void func_00298F00_inl2(RwaFreeList *fl, UnkStruct_00298F00 *block)
{
    block->link.next = fl->blockList.link.next;
    block->link.prev = &fl->blockList.link;
    fl->blockList.link.next->prev = &block->link;
    fl->blockList.link.next = &block->link;
}

// Returns an entry to its block. The block is found from the entry's address (bounds from the unaligned start of the
// entries, block + 8 + bit-map size, as retail computes them [C 0x298DB8]); a pointer in no block is ignored. The block
// moves to the front of the list; with flag 2 a block left empty is freed instead.
// 0x00298DA0
extern "C" void RwaFreeListFree(RwaFreeList *fl, void *entry)
{
    RwUInt32 mapBytes = fl->heapSize;
    UnkStruct_00298F00 *block = (UnkStruct_00298F00 *)fl->blockList.link.next;
    while (block != (UnkStruct_00298F00 *)&fl->blockList.link) {
        char *start = (char *)block + mapBytes + 8;
        if ((char *)entry >= start && (char *)entry <= start + fl->entriesPerBlock * fl->entrySize) {
            RwUInt32 index = (RwUInt32)((char *)entry - start) / fl->entrySize;
            unsigned char *map = block->bitmap;
            map[index >> 3] &= (unsigned char)~(unsigned char)(0x80 >> (index & 7));
            // unlink
            block->link.prev->next = block->link.next;
            block->link.next->prev = block->link.prev;
            if (fl->blockList.link.next != &fl->blockList.link && (fl->flags & 2)) {
                RwUInt32 used = 0;
                for (RwUInt32 k = 0; k < mapBytes; k++)
                    used += map[k];
                if (used == 0) {
                    func_00293440(block);
                    return;
                }
            }
            func_00298F00_inl2(fl, block);
            return;
        }
        block = (UnkStruct_00298F00 *)block->link.next;
    }
}

// An entry: the first clear bit of the first block that has one (blocks in list order, bytes and bits in order, at
// most entriesPerBlock entries per block); otherwise a new block, linked first, whose entry 0 is returned. Null when the new
// block cannot be allocated. `hint` goes to the allocator.
// 0x00298F00
extern "C" void *RwaFreeListAlloc(RwaFreeList *fl, RwUInt32 hint)
{
    RwUInt32 mapBytes = fl->heapSize;
    char *result = 0;
    UnkStruct_00298F00 *block = (UnkStruct_00298F00 *)fl->blockList.link.next;
    while (block != (UnkStruct_00298F00 *)&fl->blockList.link && !result) {
        RwUInt32 left = fl->entriesPerBlock;
        for (RwUInt32 b = 0; b < mapBytes; b++) {
            unsigned char *byte = &block->bitmap[b];
            unsigned char v = *byte;
            if (v == 0xFF) {
                left -= 8;
            } else {
                for (RwUInt32 bit = 0; bit < 8 && left != 0; bit++, left--) {
                    unsigned char mask = (unsigned char)(0x80 >> bit);
                    if (!(v & mask)) {
                        *byte = mask | v;
                        result = func_00298F00_inl1(fl, block) + fl->entrySize * (b * 8 + bit);
                        break;
                    }
                }
            }
            if (result)
                break;
        }
        block = (UnkStruct_00298F00 *)block->link.next;
    }
    if (result)
        return result;
    block = (UnkStruct_00298F00 *)func_002934C0(fl->alignment + mapBytes + 8 + fl->entriesPerBlock * fl->entrySize, hint);
    if (!block)
        return 0;
    memset(block->bitmap, 0, mapBytes);
    func_00298F00_inl2(fl, block);
    block->bitmap[0] = 0x80;
    return func_00298F00_inl1(fl, block);
}

// 0x002990B0
extern "C" void RwaFreeListDestroy(RwaFreeList *fl)
{
    fl->link.prev->next = fl->link.next;
    fl->link.next->prev = fl->link.prev;
    while (fl->blockList.link.next != &fl->blockList.link) {
        RwLLLink *b = fl->blockList.link.next;
        b->prev->next = b->next;
        b->next->prev = b->prev;
        func_00293440(b);
    }
    if (fl->flags & 1)
        return;
    if (_masterFreeListPtr != fl && _masterFreeListPtr)
        RwaFreeListFree(_masterFreeListPtr, fl);
    else
        func_00293440(fl);
}

// A free list of `entriesPerBlock` entries of `entrySize` bytes (rounded up to `alignment`, 0 = 1), with
// `preallocBlocks` blocks allocated now. `fl`: a caller's struct (flags 3) or null (from the master list, else the
// allocator; flags 2). Null when an allocation fails (what was allocated is freed again).
// 0x00299160
extern "C" RwaFreeList *RwaFreeListCreate(RwUInt32 entrySize, RwUInt32 entriesPerBlock, RwUInt32 alignment,
                                          RwUInt32 preallocBlocks, RwaFreeList *fl, RwUInt32 hint)
{
    if (alignment == 0)
        alignment = 1;
    if (fl) {
        fl->flags = 3;
    } else {
        if (_masterFreeListPtr)
            fl = (RwaFreeList *)RwaFreeListAlloc(_masterFreeListPtr, 0x30800);
        else
            fl = (RwaFreeList *)func_002934C0(sizeof(RwaFreeList), hint);
        if (!fl)
            return 0;
        fl->flags = 2;
    }
    RwUInt32 size = (entrySize + alignment - 1) & ~(alignment - 1);
    fl->entrySize = size;
    fl->entriesPerBlock = entriesPerBlock;
    RwUInt32 mapBytes = ((entriesPerBlock + 7) & ~7u) >> 3;
    fl->alignment = alignment;
    fl->heapSize = mapBytes;
    fl->blockList.link.next = &fl->blockList.link;
    fl->blockList.link.prev = &fl->blockList.link;
    RwUInt32 blockSize = alignment + mapBytes + 8 + entriesPerBlock * size;
    for (; preallocBlocks != 0; preallocBlocks--) {
        UnkStruct_00298F00 *block = (UnkStruct_00298F00 *)func_002934C0(blockSize, hint);
        if (!block) {
            while (fl->blockList.link.next != &fl->blockList.link) {
                RwLLLink *b = fl->blockList.link.next;
                b->prev->next = b->next;
                b->next->prev = b->prev;
                func_00293440(b);
            }
            if (!(fl->flags & 1)) {
                if (_masterFreeListPtr != fl && _masterFreeListPtr)
                    RwaFreeListFree(_masterFreeListPtr, fl);
                else
                    func_00293440(fl);
            }
            return 0;
        }
        block->link.prev = 0;
        block->link.next = 0;
        func_00298F00_inl2(fl, block);
        memset(block->bitmap, 0, mapBytes);
    }
    fl->link.next = D_004E2B80.link.next;
    fl->link.prev = &D_004E2B80.link;
    D_004E2B80.link.next->prev = &fl->link;
    D_004E2B80.link.next = &fl->link;
    return fl;
}

// Destroys every free list, then the master list.
// 0x00299360
extern "C" void _rwaFreeListModuleClose(void)
{
    while (D_004E2B80.link.next != &D_004E2B80.link)
        RwaFreeListDestroy((RwaFreeList *)((char *)D_004E2B80.link.next - 0x1C));
    RwaFreeListDestroy(_masterFreeListPtr);
    _masterFreeListPtr = 0;
    D_004E2B78 = 0;
}

// 0x002994D0
extern "C" RwBool _rwaFreeListModuleOpen(void)
{
    D_004E2B78 = 1;
    D_004E2B80.link.next = &D_004E2B80.link;
    D_004E2B80.link.prev = &D_004E2B80.link;
    _masterFreeListPtr = RwaFreeListCreate(sizeof(RwaFreeList), 0x10, 0x40, 0, &D_01E7D2C0, 0x40800);
    if (!_masterFreeListPtr) {
        D_004E2B78 = 0;
        return 0;
    }
    // the master list is not on _freeListList
    _masterFreeListPtr->link.prev->next = _masterFreeListPtr->link.next;
    _masterFreeListPtr->link.next->prev = _masterFreeListPtr->link.prev;
    return 1;
}
