// rwaheap.o (librwacore.a) -- RWA's block heaps: fixed-size blocks, each with a chunk list. Retail 0x2A0850-0x2A0F0C
// [C E map]. Not ported yet (not called in races14): RwaHeapBlockAlloc 0x2A0850, RwaHeapAlloc 0x2A0A50,
// RwaHeapDestroy 0x2A0B70.
#include "rwa/rwacore.h"

// A chunk in a block: next, prev, then 4 bytes; its data follows at +0xC [C RwaHeapFree 0x2A0964]. The block's end
// address is the list's terminator.
struct UnkStruct_002A0930_chunk {
    UnkStruct_002A0930_chunk *field_0;   // next
    UnkStruct_002A0930_chunk *field_4;   // prev
    RwUInt32 field_8;
};
// A heap block: link in the heap, the first chunk, the end [C RwaHeapCreate 0x2A0CB0-0x2A0CD4].
struct UnkStruct_002A0930_block {
    RwLLLink link;
    UnkStruct_002A0930_chunk *field_8;   // the first chunk (at block + 0x10 when created)
    UnkStruct_002A0930_chunk *field_C;   // block + block size: the chunk list's end
};
// The heap, 0x1C bytes from D_01E7D530 [C _rwaHeapModuleOpen 0x2A0EC4] (not in the stabs).
struct UnkStruct_002A0C10 {
    RwUInt32 field_0;           // 0x00 flags: 1 = the caller's struct
    RwUInt32 field_4;           // 0x04 block size
    RwUInt32 field_8;           // 0x08 extra bytes per block
    RwLinkList field_C;         // 0x0C the blocks
    RwLLLink field_14;          // 0x14 in _heapList
};

extern RwLinkList _heapList;
extern RwaFreeList D_01E7D530;   // the heaps' free list (static struct)
extern RwUInt32 D_004E2B94;      // _moduleOpen of rwaheap.o [L name, research/retail_data_names.csv]
extern "C" void *RwaFreeListAlloc(RwaFreeList *fl, RwUInt32 hint);
extern "C" void RwaFreeListFree(RwaFreeList *fl, void *entry);
extern "C" RwaFreeList *RwaFreeListCreate(RwUInt32 entrySize, RwUInt32 entriesPerBlock, RwUInt32 alignment,
                                          RwUInt32 preallocBlocks, RwaFreeList *fl, RwUInt32 hint);
extern "C" void RwaFreeListDestroy(RwaFreeList *fl);

// Frees every block of a heap and takes it off _heapList; frees the struct unless it is the caller's.
static void func_002A0C10_inl1(UnkStruct_002A0C10 *heap)
{
    while (heap->field_C.link.next != &heap->field_C.link) {
        RwLLLink *b = heap->field_C.link.next;
        b->prev->next = b->next;
        b->next->prev = b->prev;
        func_00293440(b);
    }
    heap->field_14.prev->next = heap->field_14.next;
    heap->field_14.next->prev = heap->field_14.prev;
    if (!(heap->field_0 & 1))
        RwaFreeListFree(&D_01E7D530, heap);
}

// Releases the chunk whose data is at `p`: in the block that contains p, the chunk is unlinked; a block whose chunk
// list is then empty is freed, unless it is the heap's only block (then it is linked first again). A pointer in no
// block is ignored.
// 0x002A0930
extern "C" void RwaHeapFree(UnkStruct_002A0C10 *heap, void *p)
{
    for (RwLLLink *l = heap->field_C.link.next; l != &heap->field_C.link; l = l->next) {
        UnkStruct_002A0930_block *blk = (UnkStruct_002A0930_block *)l;
        if (!((char *)blk < (char *)p) || !((char *)p < (char *)blk + heap->field_4))
            continue;
        UnkStruct_002A0930_chunk *end = blk->field_C;
        UnkStruct_002A0930_chunk *c = blk->field_8;
        do {
            if ((char *)c + 0xC == (char *)p) {
                if (c->field_4 != end)
                    c->field_4->field_0 = c->field_0;
                if (c->field_0 != end)
                    c->field_0->field_4 = c->field_4;
                break;
            }
            c = c->field_0;
        } while (c != end);
        if (blk->field_8->field_0 != blk->field_C)
            return;
        blk->link.prev->next = blk->link.next;
        blk->link.next->prev = blk->link.prev;
        if (heap->field_C.link.next != &heap->field_C.link) {
            func_00293440(blk);
            return;
        }
        blk->link.next = heap->field_C.link.next;
        blk->link.prev = &heap->field_C.link;
        heap->field_C.link.next->prev = &blk->link;
        heap->field_C.link.next = &blk->link;
        return;
    }
}

// A heap of blocks holding `count` entries of `entrySize` (+0xC chunk header each) plus `extra` bytes, `nBlocks`
// allocated now; in `heap` (the caller's, flags 1) or from the free list (flags 0). Null on failure.
// 0x002A0C10
extern "C" UnkStruct_002A0C10 *RwaHeapCreate(RwUInt32 entrySize, RwUInt32 count, RwUInt32 extra, RwUInt32 nBlocks,
                                             UnkStruct_002A0C10 *heap)
{
    if (heap) {
        heap->field_0 = 1;
    } else {
        heap = (UnkStruct_002A0C10 *)RwaFreeListAlloc(&D_01E7D530, 0x30800);
        if (!heap)
            return 0;
        heap->field_0 = 0;
    }
    heap->field_8 = extra;
    heap->field_4 = extra + ((entrySize + 0xC) * count + 0x1C);
    heap->field_C.link.next = &heap->field_C.link;
    heap->field_C.link.prev = &heap->field_C.link;
    heap->field_14.next = _heapList.link.next;
    heap->field_14.prev = &_heapList.link;
    _heapList.link.next->prev = &heap->field_14;
    _heapList.link.next = &heap->field_14;
    while (nBlocks--) {
        RwUInt32 size = heap->field_4;
        UnkStruct_002A0930_block *blk = (UnkStruct_002A0930_block *)func_002934C0(size, 0x30800);
        if (!blk) {
            func_002A0C10_inl1(heap);
            return 0;
        }
        blk->field_8 = (UnkStruct_002A0930_chunk *)((char *)blk + 0x10);
        blk->field_C = (UnkStruct_002A0930_chunk *)((char *)blk + size);
        blk->field_8->field_8 = 0;
        blk->field_8->field_4 = 0;
        blk->field_8->field_0 = blk->field_C;
        blk->link.next = heap->field_C.link.next;
        blk->link.prev = &heap->field_C.link;
        heap->field_C.link.next->prev = &blk->link;
        heap->field_C.link.next = &blk->link;
    }
    return heap;
}

// 0x002A0DF0
extern "C" void _rwaHeapModuleClose(void)
{
    while (_heapList.link.next != &_heapList.link)
        func_002A0C10_inl1((UnkStruct_002A0C10 *)((char *)_heapList.link.next - 0x14));
    RwaFreeListDestroy(&D_01E7D530);
    D_004E2B94 = 0;
}

// 0x002A0EB0
extern "C" RwBool _rwaHeapModuleOpen(void)
{
    _heapList.link.next = &_heapList.link;
    _heapList.link.prev = &_heapList.link;
    if (!RwaFreeListCreate(sizeof(UnkStruct_002A0C10), 8, 0x40, 0, &D_01E7D530, 0x40800))
        return 0;
    D_004E2B94 = 1;
    return 1;
}

// First fit in a heap block's chunk list: after each chunk, a new chunk's header goes at
// ((end + 12 + align - 1) & -align) - 12 when it and size bytes fit before the next chunk (end = chunk + 12 + its
// size); linked in after it. Returns the new chunk's data address, or 0 [C 0x2A0850-0x2A092C].
// 0x002A0850
extern "C" RwUInt32 RwaHeapBlockAlloc(UnkStruct_002A0930_block *blk, RwUInt32 size, RwUInt32 align)
{
    RwUInt32 mask = align - 1;
    RwUInt32 need = size + 12;
    UnkStruct_002A0930_chunk *cur = blk->field_8;
    UnkStruct_002A0930_chunk *cand = 0;
    for (;;) {
        UnkStruct_002A0930_chunk *next = cur->field_0;
        RwUInt32 csize = cur->field_8;
        RwUInt32 gap = ((unsigned int)next - (unsigned int)cur) - (csize + 12);
        if (!(gap < need)) {
            RwUInt32 end = (unsigned int)cur + csize + 12;
            RwUInt32 hdr = ((end + 12 + mask) & ~mask) - 12;
            if ((unsigned int)next < hdr + need) {
                cand = 0;
            } else if (hdr < end) {
                cand = 0;
            } else {
                cand = (UnkStruct_002A0930_chunk *)hdr;
                break;
            }
        }
        if (next == blk->field_C)
            break;
        cur = next;
    }
    if (!cand)
        return 0;
    cand->field_0 = cur->field_0;
    cand->field_4 = cur;
    if (cand->field_0 != blk->field_C)
        cand->field_0->field_4 = cand;
    if (cand->field_4 != blk->field_C)
        cand->field_4->field_0 = cand;
    cand->field_8 = size;
    return (unsigned int)cand + 12;
}

// The heap's blocks in order (field_8 is passed as the alignment [C 0x2A0A84]); when none fits, a new block of
// field_4 bytes (RWA allocator), linked first, takes the chunk. Returns the data address, or 0.
// 0x002A0A50
extern "C" RwUInt32 RwaHeapAlloc(UnkStruct_002A0C10 *heap, RwUInt32 size)
{
    RwLLLink *sentinel = &heap->field_C.link;
    RwUInt32 r = 0;
    RwLLLink *l = heap->field_C.link.next;
    while (l != sentinel) {
        r = RwaHeapBlockAlloc((UnkStruct_002A0930_block *)l, size, heap->field_8);
        l = l->next;
        if (r)
            break;
    }
    if (r)
        return r;
    RwUInt32 bytes = heap->field_4;
    UnkStruct_002A0930_block *n = (UnkStruct_002A0930_block *)func_002934C0(bytes, 0x30800);
    if (n) {
        n->field_8 = (UnkStruct_002A0930_chunk *)((char *)n + 0x10);
        n->field_C = (UnkStruct_002A0930_chunk *)((char *)n + bytes);
        n->field_8->field_8 = 0;
        n->field_8->field_4 = 0;
        n->field_8->field_0 = n->field_C;
    }
    if (!n)
        return r;
    n->link.next = heap->field_C.link.next;
    n->link.prev = sentinel;
    heap->field_C.link.next->prev = &n->link;
    heap->field_C.link.next = &n->link;
    return RwaHeapBlockAlloc(n, size, heap->field_8);
}

// Frees every block, unlinks the heap from _heapList and frees it unless it is the caller's.
// 0x002A0B70
extern "C" void RwaHeapDestroy(UnkStruct_002A0C10 *heap)
{
    func_002A0C10_inl1(heap);
}
