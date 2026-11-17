// rwallist.o (librwacore.a) -- RWA's linked list of data pointers: a sentinel RwaLLNode, entries RwaLLNode
// {prev, next, data} (12 bytes) from a free list. Retail 0x2997D0-0x299B00 [C E map]. Not ported yet: RwaLLNodeByteSwap 0x299890
// (not called in races14).
//
// Order [C]: iteration follows `next` from the sentinel (0x2997D0); RwaLListAddData links the new entry between the
// sentinel's `prev` neighbour and the sentinel (0x299A44-0x299A5C), i.e. at the end.
#include "rwa/rwacore.h"


extern RwaFreeList D_01E7D2F0;   // the entries' free list (static struct)
extern RwUInt32 _linkListOpen;
extern "C" RwaFreeList *RwaFreeListCreate(RwUInt32 entrySize, RwUInt32 entriesPerBlock, RwUInt32 alignment,
                                          RwUInt32 preallocBlocks, RwaFreeList *fl, RwUInt32 hint);
extern "C" void RwaFreeListDestroy(RwaFreeList *fl);

// Unlinks `node` and frees it. Retail first walks the list from the head until it meets the node or the sentinel and
// unlinks the node either way; the walk has no other effect, so it is kept only as that bound [C 0x299824 / 0x2999BC].
static void func_00299800_inl1(RwaLLNode *list, RwaLLNode *node)
{
    RwaLLNode *it = list->next;
    while (it != node && it != list)
        it = it->next;
    node->prev->next = node->next;
    node->next->prev = node->prev;
    func_00293440(node);
}

// 0x002997D0
extern "C" RwInt32 RwaLListGetCount(RwaLLNode *list)
{
    RwInt32 n = 0;
    for (RwaLLNode *it = list->next; it != list; it = it->next)
        n++;
    return n;
}

// 0x00299800
extern "C" void RwaLListEmpty(RwaLLNode *list)
{
    while (list->next != list)
        func_00299800_inl1(list, list->next);
}

// The data of the index-th entry, or null past the end.
// 0x00299900
extern "C" void *RwaLListFindDataByIndex(RwaLLNode *list, RwUInt32 index)
{
    RwaLLNode *it = list->next;
    RwUInt32 n = 0;
    while (n != index) {
        it = it->next;
        n++;
        if (it == list) {
            it = 0;
            break;
        }
    }
    if (!it)
        return 0;
    return it->data;
}

// The index of the first entry holding `data`, or -1.
// 0x00299940
extern "C" RwInt32 RwaLListFindDataIndex(RwaLLNode *list, void *data)
{
    RwInt32 n = 0;
    for (RwaLLNode *it = list->next; it != list; it = it->next) {
        if (it->data == data)
            return n;
        n++;
    }
    return -1;
}

// Removes the first entry holding `data`; the list, or null when there is none. The sentinel's own data word is
// compared too, before the end test [C 0x2999A8 reads +8 of the current node first].
// 0x00299980
extern "C" RwaLLNode *RwaLListRemoveData(RwaLLNode *list, void *data)
{
    RwaLLNode *node = list->next;
    while (node->data != data) {
        if (node == list)
            return 0;
        node = node->next;
    }
    func_00299800_inl1(list, node);
    return list;
}

// Appends `data`; the new entry, or null.
// 0x00299A10
extern "C" RwaLLNode *RwaLListAddData(RwaLLNode *list, void *data)
{
    RwaLLNode *e = (RwaLLNode *)func_002934C0(sizeof(RwaLLNode), 0x30804);
    if (!e)
        return 0;
    e->data = data;
    e->prev = list->prev;
    e->next = list;
    list->prev->next = e;
    list->prev = e;
    return e;
}

// 0x00299A80
extern "C" void _rwaLListModuleClose(void)
{
    RwaFreeListDestroy(&D_01E7D2F0);
    _linkListOpen = 0;
}

// 0x00299AB0
extern "C" RwBool _rwaLListModuleOpen(void)
{
    if (!RwaFreeListCreate(sizeof(RwaLLNode), 0x20, 0, 0, &D_01E7D2F0, 0x40804))
        return 0;
    _linkListOpen = 1;
    return 1;
}
