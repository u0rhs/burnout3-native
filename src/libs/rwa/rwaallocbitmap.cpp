// rwaallocbitmap.o (librwaspu2.a) -- allocation bitmaps: one bit per unit (1 = used), runs of units handed out as a
// handle word (start unit in the low `field_8` bits, unit count above). The "memory bitmap" puts a base address, a size
// and a unit size in front of it (SPU2 sound memory, [L] from the library). Retail 0x2B41E0-0x2B48FC [C E map].
// C port written from retail's code (audio1); every function is proven by the checker on live captures.
#include "rwa/rwacore.h"
#include "rwa/rwaspu2.h"


extern RwaFreeList D_01E835C0;   // memory bitmaps
extern RwaFreeList D_01E835F0;   // allocation bitmaps
extern "C" void *memset(void *dst, int c, unsigned int n);
extern "C" void *RwaFreeListAlloc(RwaFreeList *fl, RwUInt32 hint);
extern "C" void RwaFreeListFree(RwaFreeList *fl, void *entry);
extern "C" void _rwaAllocBitmapReserve(UnkStruct_002B4760 *bm, RwUInt32 start, RwUInt32 count, RwUInt32 used);
extern "C" RwBool _rwaAllocBitmapAlloc(UnkStruct_002B4760 *bm, RwUInt32 count, RwUInt32 *handle);

// The distance in bytes from handle h1's start unit to handle h2's.
// 0x002B41E0
extern "C" RwUInt32 _rwaMemoryBitmapHandleGetOffset(UnkStruct_002B4370 *mb, RwUInt32 h1, RwUInt32 h2)
{
    RwUInt32 mask = (1u << mb->field_0.field_8) - 1;
    return mb->field_14 * ((h2 & mask) - (h1 & mask));
}

// A handle relative to *h: start = h's start + (unit + offset - p4) / unit units, count = size / unit [C 0x2B4210].
// 0x002B4210
extern "C" void _rwaMemoryBitmapHandleGetNewHandleFromOffset(UnkStruct_002B4370 *mb, RwUInt32 *h, RwUInt32 offset,
                                                              RwUInt32 size, RwUInt32 p4, RwUInt32 *out)
{
    RwUInt32 unit = mb->field_14;
    RwUInt32 shift = mb->field_0.field_8;
    RwUInt32 start = (*h & ((1u << shift) - 1)) + (unit + (offset - p4)) / unit;
    *out = start | ((size / unit) << shift);
}

// Marks [addr, addr + size) used (or free): bytes to units, rounding down.
// 0x002B4260
extern "C" void _rwaMemoryBitmapReserve(UnkStruct_002B4370 *mb, RwUInt32 addr, RwUInt32 size, RwUInt32 used)
{
    RwUInt32 unit = mb->field_14;
    _rwaAllocBitmapReserve(&mb->field_0, (addr - mb->field_C) / unit, size / unit, used);
}

// A run for `size` bytes, rounded up to the unit (a power of two [L: the mask rounding]).
// 0x002B4290
extern "C" RwBool _rwaMemoryBitmapAlloc(UnkStruct_002B4370 *mb, RwUInt32 size, RwUInt32 *handle)
{
    RwUInt32 unit = mb->field_14;
    return _rwaAllocBitmapAlloc(&mb->field_0, ((size + (unit - 1)) & ~(unit - 1)) / unit, handle);
}

// 0x002B42C0
extern "C" void _rwaMemoryBitmapDestroy(UnkStruct_002B4370 *mb)
{
    RwBool own = 0;
    if (mb->field_0.field_9 & 1) {
        mb->field_0.field_9 &= 0xFE;
        own = 1;
    }
    if (mb->field_0.field_9 & 2)
        func_00293440(mb->field_0.field_0);
    if (mb->field_0.field_9 & 1)
        RwaFreeListFree(&D_01E835F0, mb);
    if (own)
        RwaFreeListFree(&D_01E835C0, mb);
}

extern "C" UnkStruct_002B4760 *_rwaAllocBitmapCreate(RwUInt32 units, UnkStruct_002B4760 *bm, RwUInt8 *bits);

// A failed bitmap returns the struct to D_01E835C0 even when the caller gave it [C 0x2B441C: no ownership test].
// 0x002B4370
extern "C" UnkStruct_002B4370 *_rwaMemoryBitmapCreate(RwUInt32 base, RwUInt32 size, RwUInt32 unit, UnkStruct_002B4370 *mb,
                                                    RwUInt8 *bits)
{
    RwBool own = 0;
    if (!mb) {
        mb = (UnkStruct_002B4370 *)RwaFreeListAlloc(&D_01E835C0, 0x30800);
        own = 1;
    }
    if (!mb)
        return mb;
    if (!_rwaAllocBitmapCreate(size / unit, &mb->field_0, bits)) {
        RwaFreeListFree(&D_01E835C0, mb);
        return 0;
    }
    mb->field_0.field_9 |= (own != 0);
    mb->field_14 = unit;
    mb->field_C = base;
    mb->field_10 = size;
    return mb;
}

// Free units (clear bits).
// 0x002B4450
extern "C" RwUInt32 _rwaAllocBitmapGetFree(UnkStruct_002B4760 *bm)
{
    RwUInt32 n = 0;
    for (RwUInt32 i = 0; i < bm->field_4; i++) {
        RwUInt8 b = bm->field_0[i];
        if (b == 0) {
            n += 8;
        } else if (b != 0xFF) {
            for (RwUInt8 j = 0; j < 8; j++)
                if (!((1 << j) & b))
                    n++;
        }
    }
    return n;
}

// Sets (used != 0) or clears the bits of units [start, start + count); whole bytes are stored as 0xFF / 0.
// 0x002B44E0
extern "C" void _rwaAllocBitmapReserve(UnkStruct_002B4760 *bm, RwUInt32 start, RwUInt32 count, RwUInt32 used)
{
    RwUInt32 end = start + count;
    RwUInt32 bit = used != 0;
    RwUInt8 fill = (RwUInt8)~(bit - 1);
    RwUInt32 first = start >> 3, last = end >> 3;
    for (RwUInt32 i = first; i <= last; i++) {
        RwUInt8 hi = 8, lo = 0;
        if (i == first)
            lo = start & 7;
        if (i == last)
            hi = end & 7;
        if (lo == 0 && hi == 8) {
            bm->field_0[i] = fill;
            continue;
        }
        for (RwUInt32 j = lo; j < hi; j++) {
            bm->field_0[i] &= (RwUInt8)~(1u << j);
            bm->field_0[i] |= (RwUInt8)(bit << j);
        }
    }
}

// Releases a handle's run.
// 0x002B45E0
extern "C" void _rwaAllocBitmapFree(UnkStruct_002B4760 *bm, RwUInt32 *handle)
{
    RwUInt32 shift = bm->field_8;
    RwUInt32 mask = (1u << shift) - 1;
    RwUInt32 h = *handle;
    _rwaAllocBitmapReserve(bm, h & mask, (h & ~mask) >> shift, 0);
}

// First fit: the first run of `count` clear bits, scanning bytes in order (a 0 byte counts 8 at once, so a run may
// start mid-byte and continue through whole free bytes). Marks it used and returns 1 with *handle = start | count <<
// shift; else *handle = 0 and 0.
// 0x002B4610
extern "C" RwBool _rwaAllocBitmapAlloc(UnkStruct_002B4760 *bm, RwUInt32 count, RwUInt32 *handle)
{
    RwUInt32 start = 0, run = 0;
    for (RwUInt32 i = 0; i < bm->field_4; i++) {
        RwUInt8 b = bm->field_0[i];
        if (b == 0) {
            if (run == 0)
                start = i * 8;
            run += 8;
        } else if (b == 0xFF) {
            start = i * 8;
            run = 0;
        } else {
            for (RwUInt32 j = 0; j < 8; j++) {
                if ((1u << j) & b) {
                    start = i * 8 + j;
                    run = 0;
                } else {
                    if (run == 0)
                        start = i * 8 + j;
                    run++;
                }
                if (run == count)
                    break;
            }
        }
        if (run >= count) {
            run = count;
            break;
        }
    }
    if (run < count) {
        *handle = 0;
        return 0;
    }
    _rwaAllocBitmapReserve(bm, start, run, 1);
    *handle = start | (run << bm->field_8);
    return 1;
}

// A bitmap for `units` units rounded up to 8; the struct and the bits come from the caller or are allocated here.
// 0x002B4760
extern "C" UnkStruct_002B4760 *_rwaAllocBitmapCreate(RwUInt32 units, UnkStruct_002B4760 *bm, RwUInt8 *bits)
{
    RwUInt8 flags = 0;
    RwUInt32 n = (units + 7) & ~7u;
    RwInt32 m;
    if (n & 0xFFFF0000) {
        RwUInt32 h = n >> 16;
        if (h & 0xFF00)
            m = _rwMsbBit[n >> 24] + 7;
        else
            m = _rwMsbBit[h] - 1;
        m += 0x10;
    } else if (n & 0xFF00) {
        m = _rwMsbBit[n >> 8] + 7;
    } else {
        m = _rwMsbBit[n] - 1;
    }
    RwUInt8 shift = (RwUInt8)(m + 1);
    if (!bm) {
        bm = (UnkStruct_002B4760 *)RwaFreeListAlloc(&D_01E835F0, 0x30800);
        flags |= 1;
    }
    if (!bm)
        return bm;
    if (!bits) {
        bits = (RwUInt8 *)func_002934C0(n >> 3, 0x30000);
        flags |= 2;
    }
    if (!bits) {
        if (flags & 1)
            RwaFreeListFree(&D_01E835F0, bm);
        return 0;
    }
    bm->field_0 = bits;
    bm->field_8 = shift;
    memset(bits, 0, n >> 3);
    bm->field_4 = n >> 3;
    bm->field_9 = flags;
    return bm;
}
