// rwamemory.o (librwacore.a) -- RWA's allocator interface: four function pointers at D_00670D04 and the wrappers every
// RWA object allocates through. Retail 0x293330-0x293530 [C E map; the func_ ones by position, L].
#include "rwa/rwacore.h"

extern UnkStruct_00293340 D_00670D04;
extern "C" void *memcpy(void *dst, const void *src, unsigned int n);
extern "C" void *memset(void *dst, int c, unsigned int n);
extern "C" void *malloc(unsigned int size);
extern "C" void free(void *p);
extern "C" void *realloc(void *p, unsigned int size);
extern "C" void *func_00293420(RwUInt32 size, RwUInt32 hint);
extern "C" void *func_00293410(void *p, RwUInt32 size, RwUInt32 hint);
extern "C" void *func_002933C0(RwUInt32 n, RwUInt32 size, RwUInt32 hint);

// 0x00293330
extern "C" void _rwaMemoryClose(void)
{
}

// Installs a caller's memory interface, or the default one (newlib's malloc / free / realloc and a zeroing calloc).
// 0x00293340
extern "C" RwBool func_00293340(const UnkStruct_00293340 *funcs)
{
    if (funcs) {
        memcpy(&D_00670D04, funcs, sizeof(UnkStruct_00293340));
    } else {
        D_00670D04.field_0 = func_00293420;
        D_00670D04.field_4 = free;
        D_00670D04.field_8 = func_00293410;
        D_00670D04.field_C = func_002933C0;
    }
    return 1;
}

// The default calloc: malloc(n * size), zeroed when it succeeded.
// 0x002933C0
extern "C" void *func_002933C0(RwUInt32 n, RwUInt32 size, RwUInt32 hint)
{
    RwUInt32 bytes = n * size;
    void *p = malloc(bytes);
    if (p)
        memset(p, 0, bytes);
    return p;
}

// The rest are tail calls through the interface.

// 0x00293430
extern "C" void *func_00293430(RwUInt32 n, RwUInt32 size, RwUInt32 hint)
{
    return D_00670D04.field_C(n, size, hint);
}

// 0x00293440
extern "C" void func_00293440(void *p)
{
    D_00670D04.field_4(p);
}

// Frees a block from func_00293470: the allocation's own address is stored in the word below it.
// 0x00293450
extern "C" void func_00293450(void *p)
{
    D_00670D04.field_4(*((void **)p - 1));
}

// A 64-byte aligned allocation: 64 bytes more, the start rounded up past the stored original address.
// 0x00293470
extern "C" void *func_00293470(RwUInt32 size, RwUInt32 hint)
{
    char *raw = (char *)D_00670D04.field_0(size + 0x40, hint);
    if (!raw)
        return 0;
    char *p = (char *)((((unsigned int)raw + 0x40) >> 6) << 6);
    *((char **)p - 1) = raw;
    return p;
}

// 0x002934C0
extern "C" void *func_002934C0(RwUInt32 size, RwUInt32 hint)
{
    return D_00670D04.field_0(size, hint);
}

// [C] retail 0x2934D0-0x293528 (splat's `alabel func_002934D0` inside func_002934C0's listing; read with
// mips-linux-gnu-objdump -m mips:5900): the text after the last '.' of a string (its extension), or the string
// itself when no '.' follows its first character -- the scan walks to the NUL, then back towards the start, and
// stops without testing s[0]. [L: a file-name helper of RWA's stream code; its callers are in the stream container]
// 0x002934D0
extern "C" const char *func_002934D0(const char *s)
{
    const char *p = s;
    while (*p)
        p++;
    for (;;) {
        if (*p == '.')
            return p + 1;
        p--;
        if (p == s)
            return s;
    }
}

// The default allocator calls: plain tail jumps to newlib (the hint is not used).
// 0x00293410
extern "C" void *func_00293410(void *p, RwUInt32 size, RwUInt32 hint)
{
    return realloc(p, size);
}

// 0x00293420
extern "C" void *func_00293420(RwUInt32 size, RwUInt32 hint)
{
    return malloc(size);
}
