// rwauuid.o (librwacore.a) -- UUIDs and RwaUniqueID (a UUID plus a name, either owned or borrowed).
// Retail 0x293580-0x293A58 [C E map]. Not ported yet (not called in races14): _rwamemicmp 0x293580,
// RwaUniqueIDByteSwap 0x293940, RwaUUIDByteSwap 0x2939B0.
#include "rwa/rwacore.h"

extern RwaFreeList D_01E7CF50;   // owned UUIDs (16-byte entries)
extern RwUInt32 D_004E2AF0;      // _moduleOpen of rwauuid.o [L name, research/retail_data_names.csv]
extern "C" void *memcpy(void *dst, const void *src, unsigned int n);
extern "C" void RwaFreeListFree(RwaFreeList *fl, void *entry);
extern "C" RwaFreeList *RwaFreeListCreate(RwUInt32 entrySize, RwUInt32 entriesPerBlock, RwUInt32 alignment,
                                          RwUInt32 preallocBlocks, RwaFreeList *fl, RwUInt32 hint);
extern "C" void RwaFreeListDestroy(RwaFreeList *fl);

// Copies `uuid` to `out`, byte-swapping its three integer fields when `swap` (retail does it with RwaEndianCopy into a
// stack copy; only `out` is visible).
// 0x00293650
extern "C" RwaUUID *_rwaUUIDSerialize(RwaUUID *uuid, RwaUUID *out, RwBool swap)
{
    memcpy(out, uuid, sizeof(RwaUUID));
    if (swap) {
        RwUInt32 w = out->time_low;
        out->time_low = (w << 24) | ((w & 0xFF00) << 8) | ((w & 0xFF0000) >> 8) | (w >> 24);
        RwUInt16 h = out->time_mid;
        out->time_mid = (RwUInt16)(((h & 0xFF) << 8) | ((h & 0xFF00) >> 8));
        h = out->time_hi_and_version;
        out->time_hi_and_version = (RwUInt16)(((h & 0xFF) << 8) | ((h & 0xFF00) >> 8));
    }
    return uuid;
}

// 0x00293710
extern "C" RwaUniqueID *_rwaUniqueIDInitialize(RwaUniqueID *id)
{
    id->flags = 0;
    id->name = 0;
    id->uuid = 0;
    return id;
}

// Frees what the ID owns (the pointers stay as they are).
// 0x00293730
extern "C" RwaUniqueID *_rwaUniqueIDFreeData(RwaUniqueID *id)
{
    if (id->uuid && (id->flags & 1)) {
        RwaFreeListFree(&D_01E7CF50, id->uuid);
        id->flags &= ~1u;
    }
    if (id->name && (id->flags & 2)) {
        func_00293440((void *)id->name);
        id->flags &= ~2u;
    }
    return id;
}

// 0x002937C0
extern "C" RwaUniqueID *_rwaUniqueIDAssignUUID(RwaUniqueID *id, RwaUUID *uuid)
{
    if (id->uuid && (id->flags & 1)) {
        RwaFreeListFree(&D_01E7CF50, id->uuid);
        id->flags &= ~1u;
    }
    id->uuid = uuid;
    id->flags &= ~1u;
    return id;
}

// 0x00293840
extern "C" RwaUniqueID *_rwaUniqueIDAssignName(RwaUniqueID *id, const RwChar *name)
{
    if (id->name && (id->flags & 2)) {
        func_00293440((void *)id->name);
        id->flags &= ~2u;
    }
    id->name = name;
    id->flags &= ~2u;
    return id;
}

// 0x002938C0
extern "C" void _rwaUniqueIDModuleClose(void)
{
    RwaFreeListDestroy(&D_01E7CF50);
    D_004E2AF0 = 0;
}

// 0x002938F0
extern "C" RwBool _rwaUniqueIDModuleOpen(void)
{
    if (!RwaFreeListCreate(sizeof(RwaUUID), 8, 0x40, 0, &D_01E7CF50, 0x40802))
        return 0;
    D_004E2AF0 = 1;
    return 1;
}

// 0 when equal (the same pointer, or the same 16 bytes); otherwise newlib memcmp's result, the difference of the first
// differing bytes as unsigned chars [L: newlib's memcmp contract; the checker compares v0].
// 0x00293A30
extern "C" RwInt32 RwaUUIDCompare(const RwaUUID *a, const RwaUUID *b)
{
    if (a == b)
        return 0;
    const RwUInt8 *p = (const RwUInt8 *)a, *q = (const RwUInt8 *)b;
    for (RwUInt32 i = 0; i < sizeof(RwaUUID); i++)
        if (p[i] != q[i])
            return (RwInt32)p[i] - (RwInt32)q[i];
    return 0;
}

// Compares n bytes (n = 0 wraps: until a difference), case-folded asymmetrically as retail does: a byte of `a` loses
// bit 5 when 'A' <= c < '{', a byte of `b` when d < 'B' (negative bytes included) or 'a' <= d < '{' [C 0x293584-
// 0x293610]. Returns folded a - folded b (sign-extended bytes) at the first difference, else 0.
// 0x00293580
extern "C" RwInt32 _rwamemicmp(const RwInt8 *a, const RwInt8 *b, RwUInt32 n)
{
    do {
        RwInt32 c = *a;
        RwInt32 d = *b;
        if (c >= 0x41 && c < 0x7B)
            c = (RwInt8)(c & ~0x20);
        if (d < 0x42 || (d >= 0x61 && d < 0x7B))
            d = (RwInt8)(d & ~0x20);
        if (c != d)
            return c - d;
        a++;
        b++;
    } while (--n);
    return 0;
}
