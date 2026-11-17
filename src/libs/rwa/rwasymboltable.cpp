// rwasymboltable (librwacore.a, truncated in the E map) -- a counted list of parameter types (RwaParamType::link).
// Retail 0x291A50-0x291C34 [C E map].
#include "rwa/rwacore.h"

// The table: a count and the list (not in the stabs) [C 0x291C20].
struct UnkStruct_00291C20 {
    RwInt32 count;
    RwLinkList list;
};

extern "C" RwInt32 RwaUUIDCompare(const RwaUUID *a, const RwaUUID *b);

static RwaParamType *func_00291A50_inl1(RwLLLink *l)
{
    return (RwaParamType *)((char *)l - 0x20);
}

// The index-th type (0 = the first); null past the end or when the table is empty or shorter than index.
// 0x00291A50
extern "C" RwaParamType *_rwaSymbolTableFindParamTypeByIndex(UnkStruct_00291C20 *table, RwInt32 index)
{
    if (table->count < index || table->count == 0)
        return 0;
    RwLLLink *l = table->list.link.next;
    RwaParamType *pt = func_00291A50_inl1(l);
    for (RwInt32 i = 0; i < index; i++) {
        l = l->next;
        if (l == &table->list.link)
            return 0;
        pt = func_00291A50_inl1(l);
    }
    return pt;
}

// The first type that depends on `dep`, or null. Retail tests the first element before the end test, as here.
// 0x00291AB0
extern "C" RwaParamType *_rwaSymbolTableFindParamTypeFirstDepend(UnkStruct_00291C20 *table, RwaParamType *dep)
{
    RwLLLink *l = table->list.link.next;
    RwaParamType *pt = func_00291A50_inl1(l);
    while (pt->paramType != dep) {
        l = l->next;
        if (l == &table->list.link)
            return 0;
        pt = func_00291A50_inl1(l);
    }
    return pt;
}

// The type with this UUID, or null (first element compared before the end test, as retail does).
// 0x00291AF0
extern "C" RwaParamType *_rwaSymbolTableFindParamTypeByUUID(UnkStruct_00291C20 *table, const RwaUUID *uuid)
{
    RwLLLink *l = table->list.link.next;
    RwaParamType *pt = func_00291A50_inl1(l);
    while (RwaUUIDCompare(pt->uniqueID.uuid, uuid) != 0) {
        l = l->next;
        if (l == &table->list.link)
            return 0;
        pt = func_00291A50_inl1(l);
    }
    return pt;
}

// Unlinks `pt` (retail first walks the list to it; the walk has no effect) and counts it out.
// 0x00291B60
extern "C" UnkStruct_00291C20 *_rwaSymbolTableRemoveParamType(UnkStruct_00291C20 *table, RwaParamType *pt)
{
    table->count = table->count - 1;
    pt->link.prev->next = pt->link.next;
    pt->link.next->prev = pt->link.prev;
    return table;
}

// 0x00291BC0
extern "C" UnkStruct_00291C20 *_rwaSymbolTableAddParamType(UnkStruct_00291C20 *table, RwaParamType *pt)
{
    pt->link.prev = 0;
    pt->link.next = 0;
    pt->link.next = table->list.link.next;
    pt->link.prev = &table->list.link;
    table->list.link.next->prev = &pt->link;
    table->list.link.next = &pt->link;
    table->count = table->count + 1;
    return table;
}

// 0x00291C00
extern "C" void _rwaSymbolTableDestroy(UnkStruct_00291C20 *table)
{
    table->count = 0;
    table->list.link.next = &table->list.link;
    table->list.link.prev = &table->list.link;
}

// 0x00291C20
extern "C" void _rwaSymbolTableInit(UnkStruct_00291C20 *table)
{
    table->count = 0;
    table->list.link.next = &table->list.link;
    table->list.link.prev = &table->list.link;
}
