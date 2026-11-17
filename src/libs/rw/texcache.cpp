#include "rw/skyglobals.h"
// RenderWare 3.6 sky2 driver: the texture cache (RW 3.1 file name: driver/sky2/texcache.c; reading aid only).
// Written from the retail asm (decomp assembly/asm/rw/renderware.s). Tags: [C] confirmed from the asm, [L], [G].
// boot1 staging (rw_wip); boot2: skyTexCacheOpenCache.

// [C] one 0xC-byte entry of skyTexCache.field_14.
struct UnkStruct_skyTexCacheEntry {
    void *field_0;                      // [C] a raster, or 0
    unsigned int field_4;               // [C] the GS word address of the slot (RpSkyTexCacheSetStartAddress, AccessSpeculate)
    unsigned char pad_8[0x4];
};

// [C] skyTexCache: 0x3C bytes (symbol table). Ring indices field_8 .. field_4 modulo field_10; field_10 .. field_C the
// entries past the ring ([L] meanings).
struct UnkStruct_skyTexCache {
    int field_0;                            // [C] tested first; set to 1 on close
    unsigned int field_4;                   // [C] ring end
    unsigned int field_8;                   // [C] ring start
    unsigned int field_C;                   // [C] entry count
    unsigned int field_10;                  // [C] ring size (divu divisor; break 7 when 0)
    UnkStruct_skyTexCacheEntry *field_14;   // [C] entries, freed through ourGlobals + 0x138
    int field_18;                           // [C] -1 on close
    int field_1C;                           // [C] -1 on close
    int field_20;
    int field_24;
    int field_28;
    int field_2C;                           // [C] -1 on open
    int field_30;                           // [C] -1 on open
    int field_34;                           // [C] -1 on close
    int field_38;
};
extern UnkStruct_skyTexCache skyTexCache;   // [C] 0x0049B040

struct UnkStruct_ourGlobals {
    unsigned char pad_0[0x134];
    void *(*field_134)(unsigned int size, unsigned int hint);   // [C] the malloc function ([L] memoryFuncs.rwmalloc)
    void (*field_138)(void *);              // [C] the free function ([L] memoryFuncs.rwfree)
};
extern UnkStruct_ourGlobals ourGlobals;
extern int skyRasterExt;                    // [C] gp-relative: the raster plugin's offset

// [C] while the cache is set up (field_0 != 0): the rasters of ring entries field_8 .. field_4 (exclusive, modulo
// field_10; skyRasterExt read once) get word +0x58 of their extension cleared; the entries field_10 .. field_C - 1
// (field_C re-read each pass, skyRasterExt too) get word +0x58 and byte +0x17 cleared; then field_14 is freed. Then
// unconditionally the cache is reset: field_0 = 1, field_18 = field_1C = field_34 = -1, the rest 0. No defined result.
// 0x0020CD08
extern "C" void skyTexCacheCloseCache(void)
{
    unsigned int i;
    unsigned int end;
    int ext;
    void *raster;

    if (skyTexCache.field_0 != 0) {
        i = skyTexCache.field_8;
        end = skyTexCache.field_4;
        if (i != end) {
            ext = skyRasterExt;
            do {
                raster = skyTexCache.field_14[i].field_0;
                if (raster != 0) {
                    *(int *)((char *)raster + ext + 0x58) = 0;
                }
                i = (i + 1) % skyTexCache.field_10;
            } while (i != end);
        }
        for (i = skyTexCache.field_10; i < skyTexCache.field_C; i++) {
            raster = skyTexCache.field_14[i].field_0;
            if (raster != 0) {
                *(int *)((char *)raster + skyRasterExt + 0x58) = 0;
                *((char *)raster + skyRasterExt + 0x17) = 0;
            }
        }
        ourGlobals.field_138(skyTexCache.field_14);
    }
    skyTexCache.field_1C = -1;
    skyTexCache.field_0 = 1;
    skyTexCache.field_38 = 0;
    skyTexCache.field_4 = 0;
    skyTexCache.field_8 = 0;
    skyTexCache.field_C = 0;
    skyTexCache.field_10 = 0;
    skyTexCache.field_14 = 0;
    skyTexCache.field_18 = -1;
    skyTexCache.field_34 = -1;
    skyTexCache.field_20 = 0;
    skyTexCache.field_24 = 0;
}

// [C] the GS A+D template for image uploads: 4 qwords (TEXFLUSH, TRXPOS, TRXDIR registers, then a GIF tag)
struct UnkStruct_texcacheState {
    unsigned long long field_0;
    unsigned long long field_8;
    unsigned long long field_10;
    unsigned long long field_18;
    unsigned long long field_20;
    unsigned long long field_28;
    unsigned long long field_30;
    unsigned long long field_38;
};
extern UnkStruct_texcacheState state;       // [C] 0x006763C0
extern unsigned int D_004E1900;             // [C] gp-relative, _rwDMAInAsyncPkt.35 in the table: the entry count (0xC4)
extern unsigned int D_004E1904;             // [C] gp-relative, _rwDMAInAsyncPkt.53 in the table: a texture cache counter
extern "C" void _skyTexCacheUploadRaster(void);
extern "C" void func_0020E4E0(void);

// [C] the cache covers GS words start .. start + size; when it is set up (field_0, 1 in the image) the entry array
// (D_004E1900 entries of 0xC bytes) comes from the RW allocator and the ring is reset, with _skyTexCacheUploadRaster
// as the upload function, else func_0020E4E0. Then the upload template. Returns 0 only when the allocation fails
// (the template is then not written), else 1.
// 0x0020CC08
extern "C" int skyTexCacheOpenCache(unsigned int start, unsigned int size)
{
    unsigned int n;
    UnkStruct_skyTexCacheEntry *entries;

    skyTexCache.field_34 = start;
    skyTexCache.field_1C = size;
    skyTexCache.field_18 = start;
    D_004E1904 = 0;
    if (skyTexCache.field_0 != 0) {
        entries = (UnkStruct_skyTexCacheEntry *)ourGlobals.field_134(D_004E1900 * 0xC, 0x40411);
        skyTexCache.field_14 = entries;
        if (entries == 0) {
            return 0;
        }
        n = D_004E1900;
        skyTexCache.field_30 = -1;
        skyTexCache.field_10 = n;
        skyTexCache.field_20 = (int)_skyTexCacheUploadRaster;
        skyTexCache.field_4 = 0xFFFFFFFF;
        skyTexCache.field_8 = 0xFFFFFFFF;
        skyTexCache.field_C = n;
        skyTexCache.field_24 = 0;
        skyTexCache.field_2C = -1;
    } else {
        skyTexCache.field_20 = (int)func_0020E4E0;
    }
    state.field_20 = 0;
    state.field_28 = 0x53;          // TRXDIR
    state.field_30 = 0x1000000000008005ULL;   // GIFtag NLOOP 5, EOP, PACKED, NREG 1
    state.field_38 = 0xE;           // A+D
    state.field_0 = 0;
    state.field_8 = 0x3F;           // TEXFLUSH
    state.field_10 = 0;
    state.field_18 = 0x51;          // TRXPOS
    return 1;
}

// ---- the RpSkyTexCache* API (boot2) ----

// [C] the raster fields read here ([L] RwRaster: +4 cpPixels, +0x20 cType)
struct UnkStruct_texcacheRaster {
    unsigned char pad_0[0x4];
    unsigned int field_4;               // [C] tested for 0 ([L] cpPixels)
    unsigned char pad_8[0x18];
    unsigned char field_20;             // [C] bit 0x80 tested ([L] cType rwRASTERDONTALLOCATE)
};
// [C] the driver's raster extension at raster + skyRasterExt (the offsets used here)
struct UnkStruct_texcacheRasterExt {
    unsigned char pad_0[0x8];
    unsigned int field_8;               // [C] TBP0 in bits 0-13 ([L] TEX0 low)
    unsigned int field_C;               // [C] CBP in bits 5-18 ([L] TEX0 high)
    unsigned int field_10;              // [C] the palette's offset from the texels (GS words / 64)
    unsigned short field_14;            // [C] TEX1 K (bits 0-11) / L (bits 12-13)
    unsigned char field_16;             // [C] TEX1's low byte
    unsigned char field_17;             // [C] cleared by RasterUnlockAll ([L] locked)
    unsigned int field_18;
    unsigned int field_1C;
    unsigned int field_20;
    unsigned int field_24;
    unsigned char pad_28[0x30];
    unsigned int field_58;              // [C] the cache entry holding the raster, or 0 (also stored as 0 / 1 by func_0020E230)
};
extern "C" int skyTexCacheMemRasterToGs(void *raster, unsigned int dst, int flag);
extern "C" void _SkyUpLoadAlignedRectangle(unsigned int pixelWidth, unsigned int pixelHeight, unsigned int src,
                                           unsigned int dst, unsigned int dstWidthBy64, unsigned int bitsPerPixel,
                                           unsigned int pixelFormat, unsigned int srcStride);

static UnkStruct_texcacheRasterExt *texcacheExt(void *raster, int ext)
{
    return (UnkStruct_texcacheRasterExt *)((char *)raster + ext);
}

// [C] gives a raster's slot back (handwritten-looking ring code, [C 0x20D5E8..0x20D7E0]). With the cache set up and
// the raster in a slot: the slot's raster word cleared, its index (by the 0xAAAAAAAB reciprocal) clears field_2C /
// field_30 when equal (to -1); a slot at the ring end field_4 (and not at field_8) moves field_4 back over empty
// slots (wrapping 0 -> field_10, stopping at field_8); a slot at the ring start field_8 (and not at field_4) moves
// field_8 on over empty slots (wrapping field_10 -> 0, stopping at field_4); then when the entry at field_4 is empty
// the ring is reset (-1, -1). Cache not set up: the release callback field_38 (when set and the raster had a slot)
// is called with the raster. Always: ext +0x58 = 0, field_24 = 0 when it is this raster. No defined result.
// 0x0020D5E8
extern "C" void RpSkyTexCacheReleaseRaster(void *raster)
{
    UnkStruct_texcacheRasterExt *x = texcacheExt(raster, skyRasterExt);
    UnkStruct_skyTexCacheEntry *e;
    int idx;
    unsigned int end;
    unsigned int start;
    unsigned int i;

    if (skyTexCache.field_0 != 0) {
        e = (UnkStruct_skyTexCacheEntry *)x->field_58;
        if (e != 0) {
            e->field_0 = 0;
            idx = (int)((unsigned int)((char *)e - (char *)skyTexCache.field_14) * 0xAAAAAAABu) >> 2;
            if (idx == skyTexCache.field_2C) {
                skyTexCache.field_2C = -1;
            }
            if (idx == skyTexCache.field_30) {
                skyTexCache.field_30 = -1;
            }
            end = skyTexCache.field_4;
            if ((unsigned int)idx == end) {
                start = skyTexCache.field_8;
                if ((unsigned int)idx != start && skyTexCache.field_14[idx].field_0 == 0) {
                    i = skyTexCache.field_4;
                    for (;;) {
                        if (i == 0) {
                            skyTexCache.field_4 = skyTexCache.field_10;
                            i = skyTexCache.field_4;
                        }
                        i = i - 1;
                        skyTexCache.field_4 = i;
                        if (i == start) {
                            break;
                        }
                        if (skyTexCache.field_14[(int)i].field_0 != 0) {
                            break;
                        }
                        i = skyTexCache.field_4;
                    }
                }
            } else if ((unsigned int)idx == skyTexCache.field_8) {
                if (end != (unsigned int)idx && skyTexCache.field_14[idx].field_0 == 0) {
                    i = skyTexCache.field_8;
                    for (;;) {
                        i = i + 1;
                        skyTexCache.field_8 = i;
                        if (i == skyTexCache.field_10) {
                            skyTexCache.field_8 = 0;
                        }
                        i = skyTexCache.field_8;
                        if (end == i) {
                            break;
                        }
                        if (skyTexCache.field_14[(int)i].field_0 != 0) {
                            break;
                        }
                        i = skyTexCache.field_8;
                    }
                }
            }
            if (skyTexCache.field_14[(int)skyTexCache.field_4].field_0 == 0) {
                skyTexCache.field_8 = 0xFFFFFFFF;
                skyTexCache.field_4 = 0xFFFFFFFF;
            }
        }
    } else if (skyTexCache.field_38 != 0 && x->field_58 != 0) {
        ((void (*)(void *))skyTexCache.field_38)(raster);
    }
    x->field_58 = 0;
    if (raster == (void *)skyTexCache.field_24) {
        skyTexCache.field_24 = 0;
    }
}

// [C] the ring field_8 .. field_4 walked as by Flush: each entry's raster (if any) gets ext +0x58 cleared and the
// entry's raster word cleared; field_10 read once, field_14 and field_4 each pass, skyRasterExt once.
static void texcacheEmptyRing(void)
{
    unsigned int i;
    unsigned int n;
    int ext;
    void *r;

    i = skyTexCache.field_8;
    n = skyTexCache.field_10;
    ext = skyRasterExt;
    for (;;) {
        unsigned int k;

        if (i == n) {
            i = 0;
        }
        r = skyTexCache.field_14[(int)i].field_0;
        if (r != 0) {
            texcacheExt(r, ext)->field_58 = 0;
            skyTexCache.field_14[(int)i].field_0 = 0;
        }
        k = i;
        i = i + 1;
        if (k == skyTexCache.field_4) {
            break;
        }
    }
}

// [C] moves the cache's start to addr rounded up to 0x800 GS words. Refused (returns 0) before the cache was opened
// (field_18 == -1), when it is disabled (field_0 == 0), or when the new start is below field_34 or not below the
// limit (the address of entry field_10 when field_10 != field_C, else field_18 + field_1C; signed compares). Then
// the ring is emptied (when field_4 != -1) and reset to -1 / -1, skyTextureRaster = 0, field_18 = new start, field_24
// = 0, field_1C shrinks by the move. Returns 1.
// 0x0020DD08
extern "C" int RpSkyTexCacheSetStartAddress(unsigned int addr)
{
    unsigned int start;
    int limit;
    int old;

    if (skyTexCache.field_18 == -1) {
        return 0;
    }
    if (skyTexCache.field_0 == 0) {
        return 0;
    }
    start = (addr + 0x7FF) & 0xFFFFF800;
    if (skyTexCache.field_10 != skyTexCache.field_C) {
        limit = (int)skyTexCache.field_14[(int)skyTexCache.field_10].field_4;
    } else {
        limit = skyTexCache.field_18 + skyTexCache.field_1C;
    }
    if ((int)start < skyTexCache.field_34) {
        return 0;
    }
    if (!((int)start < limit)) {
        return 0;
    }
    if (skyTexCache.field_0 != 0) {
        if (skyTexCache.field_4 != 0xFFFFFFFF) {
            texcacheEmptyRing();
        }
        skyTexCache.field_8 = 0xFFFFFFFF;
        skyTexCache.field_4 = 0xFFFFFFFF;
    }
    skyTextureRaster = 0;
    old = skyTexCache.field_18;
    skyTexCache.field_18 = (int)start;
    skyTexCache.field_24 = 0;
    skyTexCache.field_1C = skyTexCache.field_1C - ((int)start - old);
    return 1;
}

// [C] for a raster that is not the current one (field_24), is allocated (cType bit 0x80 clear), has pixels and no
// slot: the upload callback field_20(raster, 1); when that gave it a slot and the cache is set up, TBP0 = slot
// address >> 6 and CBP = (that + ext +0x10) & 0x3FFF. No defined result.
// 0x0020DE40
extern "C" void RpSkyTexCacheAccessSpeculate(void *raster)
{
    UnkStruct_texcacheRasterExt *x = texcacheExt(raster, skyRasterExt);
    UnkStruct_texcacheRaster *r = (UnkStruct_texcacheRaster *)raster;
    UnkStruct_skyTexCacheEntry *e;
    unsigned int v;

    if (raster == (void *)skyTexCache.field_24) {
        return;
    }
    if ((r->field_20 & 0x80) != 0) {
        return;
    }
    if (r->field_4 == 0) {
        return;
    }
    if (x->field_58 != 0) {
        return;
    }
    ((void (*)(void *, int))skyTexCache.field_20)(raster, 1);
    e = (UnkStruct_skyTexCacheEntry *)x->field_58;
    if (e == 0) {
        return;
    }
    if (skyTexCache.field_0 == 0) {
        return;
    }
    v = e->field_4 >> 6;
    x->field_8 = (x->field_8 & 0xFFFFC000) | (v & 0x3FFF);
    x->field_C = (x->field_C & 0xFFF8001F) | (((v + x->field_10) & 0x3FFF) << 5);
}

// [C] with the cache set up: every entry field_10 .. field_C - 1 (field_C re-read) that holds a raster gets ext byte
// +0x17 and +0x58 cleared, field_24 = 0 when it is that raster, and the entry's raster word cleared; then field_10 =
// field_C. No defined result.
// 0x0020DF10
extern "C" void RpSkyTexCacheRasterUnlockAll(void)
{
    unsigned int i;
    void *r;
    UnkStruct_texcacheRasterExt *x;

    if (skyTexCache.field_0 == 0) {
        return;
    }
    for (i = skyTexCache.field_10; i < skyTexCache.field_C; i++) {
        r = skyTexCache.field_14[i].field_0;
        if (r != 0) {
            x = texcacheExt(r, skyRasterExt);
            x->field_17 = 0;
            x->field_58 = 0;
            if (skyTexCache.field_14[i].field_0 == (void *)skyTexCache.field_24) {
                skyTexCache.field_24 = 0;
            }
            skyTexCache.field_14[i].field_0 = 0;
        }
    }
    skyTexCache.field_10 = skyTexCache.field_C;
}

// [C] with the cache set up: the ring emptied (when field_4 != -1) and reset to -1 / -1; then always
// skyTextureRaster = 0 and field_24 = 0. No defined result.
// 0x0020DFB0
extern "C" void RpSkyTexCacheFlush(void)
{
    if (skyTexCache.field_0 != 0) {
        if (skyTexCache.field_4 != 0xFFFFFFFF) {
            texcacheEmptyRing();
        }
        skyTexCache.field_8 = 0xFFFFFFFF;
        skyTexCache.field_4 = 0xFFFFFFFF;
    }
    skyTextureRaster = 0;
    skyTexCache.field_24 = 0;
}

// [C] only before the cache was opened (field_18 == -1; else returns 0): field_0 = (disable == 0). Returns 1.
// 0x0020E050
extern "C" int RpSkyTexCacheDisable(int disable)
{
    if (skyTexCache.field_18 != -1) {
        return 0;
    }
    skyTexCache.field_0 = (disable == 0);
    return 1;
}

// 0x0020E080
extern "C" int func_0020E080(void)
{
    return skyTexCache.field_18;
}

// 0x0020E090
extern "C" int func_0020E090(void)
{
    return skyTexCache.field_1C;
}

// [C] field_20 = the upload callback (a guest function address). Returns 1.
// 0x0020E0A0
extern "C" int RpSkyTexCacheSetCallBack(unsigned int func)
{
    skyTexCache.field_20 = (int)func;
    return 1;
}

// 0x0020E0B0
extern "C" int func_0020E0B0(void)
{
    return skyTexCache.field_20;
}

// [C] field_38 = the release callback (a guest function address). Returns 1.
// 0x0020E0C0
extern "C" int RpSkyTexCacheSetReleaseCallBack(unsigned int func)
{
    skyTexCache.field_38 = (int)func;
    return 1;
}

// 0x0020E0D0
extern "C" int func_0020E0D0(void)
{
    return skyTexCache.field_38;
}

// [C] ext +8 & 0x3FFF ([L] the raster's TBP0)
// 0x0020E0E0
extern "C" unsigned int func_0020E0E0(void *raster)
{
    return texcacheExt(raster, skyRasterExt)->field_8 & 0x3FFF;
}

// [C] ext +0xC = a1, +8 = a2; returns the raster.
// 0x0020E0F8
extern "C" void *func_0020E0F8(void *raster, unsigned int c, unsigned int e)
{
    UnkStruct_texcacheRasterExt *x = texcacheExt(raster, skyRasterExt);

    x->field_C = c;
    x->field_8 = e;
    return raster;
}

// [C] *a2 = ext +8, *a1 = ext +0xC; returns the raster.
// 0x0020E110
extern "C" void *func_0020E110(void *raster, unsigned int *c, unsigned int *e)
{
    UnkStruct_texcacheRasterExt *x = texcacheExt(raster, skyRasterExt);

    *e = x->field_8;
    *c = x->field_C;
    return raster;
}

// [C] (handwritten) a TEX1 value into *out (64-bit): ext byte +0x16 | L (halfword +0x14 bits 12-13) << 19 | K (bits
// 0-11) << 32, ORed with the filter bits for mode: < 2 none, 2 0x60, 3 0x80, 4 0x120, 6 0x160, 5 or > 6 0xC0.
// Returns the raster. [C] the asm's `addi` mode - 2 / - 4 / - 6 would trap on overflow (mode near INT_MIN): not
// expressed.
// 0x0020E130
extern "C" void *func_0020E130(void *raster, unsigned long long *out, int mode)
{
    UnkStruct_texcacheRasterExt *x = texcacheExt(raster, skyRasterExt);
    unsigned int hw = x->field_14;
    unsigned long long v;

    v = (unsigned long long)x->field_16 | ((unsigned long long)(hw & 0xFFF) << 32)
        | ((unsigned long long)((hw >> 12) & 3) << 19);
    if (mode == 2) {
        v |= 0x60;
    } else if (mode < 2) {
        v |= 0;
    } else if (mode == 4) {
        v |= 0x120;
    } else if (mode < 4) {
        v |= 0x80;
    } else if (mode == 6) {
        v |= 0x160;
    } else {
        v |= 0xC0;
    }
    *out = v;
    return raster;
}

// [C] ext +0x1C = a1, +0x18 = a2; returns the raster.
// 0x0020E1A8
extern "C" void *func_0020E1A8(void *raster, unsigned int a, unsigned int b)
{
    UnkStruct_texcacheRasterExt *x = texcacheExt(raster, skyRasterExt);

    x->field_1C = a;
    x->field_18 = b;
    return raster;
}

// [C] *a2 = ext +0x18, *a1 = ext +0x1C; returns the raster.
// 0x0020E1C0
extern "C" void *func_0020E1C0(void *raster, unsigned int *a, unsigned int *b)
{
    UnkStruct_texcacheRasterExt *x = texcacheExt(raster, skyRasterExt);

    *b = x->field_18;
    *a = x->field_1C;
    return raster;
}

// [C] ext +0x24 = a3, +0x18 = a2, +0x1C = a1, +0x20 = t0 (in that order); returns the raster.
// 0x0020E1E0
extern "C" void *func_0020E1E0(void *raster, unsigned int a, unsigned int b, unsigned int c, unsigned int d)
{
    UnkStruct_texcacheRasterExt *x = texcacheExt(raster, skyRasterExt);

    x->field_24 = c;
    x->field_18 = b;
    x->field_1C = a;
    x->field_20 = d;
    return raster;
}

// [C] *a2 = ext +0x18, *a1 = ext +0x1C, *t0 = ext +0x20, *a3 = ext +0x24; returns the raster.
// 0x0020E200
extern "C" void *func_0020E200(void *raster, unsigned int *a, unsigned int *b, unsigned int *c, unsigned int *d)
{
    UnkStruct_texcacheRasterExt *x = texcacheExt(raster, skyRasterExt);

    *b = x->field_18;
    *a = x->field_1C;
    *d = x->field_20;
    *c = x->field_24;
    return raster;
}

// [C] only while the cache is disabled (field_0 == 0; else returns 0): ext +0x58 = (valid != 0); returns the raster
// ([L] RpSkyTexCacheValidateRaster).
// 0x0020E230
extern "C" void *func_0020E230(void *raster, int valid)
{
    if (skyTexCache.field_0 != 0) {
        return 0;
    }
    texcacheExt(raster, skyRasterExt)->field_58 = (valid != 0);
    return raster;
}

// [C] _SkyUpLoadAlignedRectangle with all eight arguments when src is 16-byte aligned; returns 1 either way.
// 0x0020E260
extern "C" int RpSkyUploadPixelData(unsigned int pixelWidth, unsigned int pixelHeight, unsigned int src,
                                    unsigned int dst, unsigned int dstWidthBy64, unsigned int bitsPerPixel,
                                    unsigned int pixelFormat, unsigned int srcStride)
{
    if ((src & 0xF) == 0) {
        _SkyUpLoadAlignedRectangle(pixelWidth, pixelHeight, src, dst, dstWidthBy64, bitsPerPixel, pixelFormat,
                                   srcStride);
    }
    return 1;
}

// [C] skyTexCacheMemRasterToGs(raster, dst, 0), then CBP = (dst + ext +0x10) & 0x3FFF and TBP0 = dst & 0x3FFF
// (skyRasterExt read after the call; +0xC stored first). Returns the raster.
// 0x0020E288
extern "C" void *RpSkyTexCacheUploadRaster(void *raster, unsigned int dst)
{
    UnkStruct_texcacheRasterExt *x;
    unsigned int c;
    unsigned int t;

    skyTexCacheMemRasterToGs(raster, dst, 0);
    x = texcacheExt(raster, skyRasterExt);
    c = (x->field_C & 0xFFF8001F) | (((dst + x->field_10) & 0x3FFF) << 5);
    t = (x->field_8 & 0xFFFFC000) | (dst & 0x3FFF);
    x->field_C = c;
    x->field_8 = t;
    return raster;
}

// 0x0020E310
extern "C" int RpSkyTexCacheGetStartAddress(void)
{
    return skyTexCache.field_18;
}

// [C] an empty function (jr ra; nop).
// 0x0020E320
extern "C" void skyTexCacheEndFrame(void)
{
}

// [C] an empty function (jr ra; nop): the upload callback of a disabled cache (skyTexCacheOpenCache).
// 0x0020E4E0
extern "C" void func_0020E4E0(void)
{
}

// ---------------------------------------------------------------------------------------------------------------
// boot2 additions (track rw): RpSkyTexCacheRestore.

struct UnkStruct_texRestoreQword {
    unsigned int field_0;
    unsigned int field_4;
    unsigned int field_8;
    unsigned int field_C;
};
// [C] _rwDMAFlipData read as 64-bit words (see basky_fb.cpp)
struct UnkStruct_texRestoreFlip {
    unsigned long long q[0x7A];
};
// [C] the raster as the restore reads it: +0x20 bits 0-2 the type
struct UnkStruct_texRestoreRaster {
    unsigned char pad_0[0x20];
    unsigned char field_20;
};
// [C] the driver's raster extension at raster + skyRasterExt (offsets from the asm)
struct UnkStruct_texRestoreExt {
    unsigned char pad_0[0x8];
    unsigned int field_8;              // [C] TEX0 low (TBP0 bits 0-13, ...)
    unsigned int field_C;              // [C] TEX0 high
    unsigned char pad_10[0x4];
    unsigned short field_14;           // [C] TEX1 bits (K, L)
    unsigned char field_16;
    unsigned char pad_17[0x1];
    unsigned int field_18;             // [C] MIPTBP1 low
    unsigned int field_1C;             // [C] MIPTBP1 high
    unsigned int field_20;             // [C] MIPTBP2 low
    unsigned int field_24;             // [C] MIPTBP2 high
};

extern int skyFrameBit;                           // [C] gp 0x004E188C
extern unsigned long long skyTex1_1;              // [C] gp 0x004E1860 (TEX1_1 shadow, 8 bytes)
extern UnkStruct_texRestoreFlip _rwDMAFlipData;   // [C] 0x006754C0
extern "C" int _rwDMAOpenVIFPkt(unsigned int type, unsigned int nQW);

// [C] when the cache holds a raster (skyTexCache.field_24, a raster pointer kept in an int; field_28 selects the
// context: 0 -> TEX0_1 / TEX1_1 / MIPTBP*_1 registers 6 / 0x14 / 0x34 / 0x36, else TEX0_2 7, TEX1_2 0x15, MIPTBP1_2 0x35,
// MIPTBP2_2 0x37): a VIF packet of 0x20 qwords (type bit 31) is opened and a GIFtag {NLOOP 4, EOP, A+D} written, then
// TEX0 (the extension's +8 / +0xC words; for a raster of type 2 the low bits are replaced by (flip data word & 0x1FF)
// << 5 added to the TBP: word 0x1D0 / 8 when the frame bit is set, 0x60 / 8 otherwise [C: opposite of
// SkyCameraBeginUpdate]), TEX1 (skyTex1_1 with the K / L bits replaced from +0x14 / +0x16, also saved back), then
// MIPTBP1 and MIPTBP2 from +0x18 / +0x1C and +0x20 / +0x24; when the cache is enabled (skyTexCache.field_0 != 0) the
// TBP replicated into bits 0, 20 and 40 (TBP0 = +8 & 0x3FFF) is added to both.
// 0x0020DAC0
extern "C" void RpSkyTexCacheRestore(void)
{
    UnkStruct_texRestoreRaster *raster;
    UnkStruct_texRestoreExt *ext;
    UnkStruct_texRestoreQword *q;
    unsigned long long a2;
    unsigned long long tbp;
    unsigned long long v;
    unsigned int cur;
    unsigned int ctx;
    unsigned int word;

    raster = (UnkStruct_texRestoreRaster *)(long)skyTexCache.field_24;
    if (raster == 0) {
        return;
    }
    ctx = (unsigned int)skyTexCache.field_28;
    ext = (UnkStruct_texRestoreExt *)((unsigned char *)raster + skyRasterExt);
    _rwDMAOpenVIFPkt(0x80000000, 0x20);
    q = (UnkStruct_texRestoreQword *)farFogPlane;
    q->field_0 = 0x8004;
    q->field_4 = 0x10000000;
    q->field_8 = 0xE;
    q->field_C = 0;
    farFogPlane = farFogPlane + 0x10;
    a2 = (unsigned long long)ext->field_8 | ((unsigned long long)ext->field_C << 32);
    if ((raster->field_20 & 7) == 2) {
        if ((skyFrameBit & 1) == 0) {
            v = _rwDMAFlipData.q[0x60 / 8];
        } else {
            v = _rwDMAFlipData.q[0x1D0 / 8];
        }
        word = ext->field_8;
        a2 = (a2 & 0xFFFFFFFFFFFFC000ULL) | (unsigned long long)((word & 0x3FFF) + ((v & 0x1FF) << 5));
    }
    cur = farFogPlane;
    q = (UnkStruct_texRestoreQword *)cur;
    q->field_0 = (unsigned int)a2;
    q->field_4 = (unsigned int)(a2 >> 32);
    q->field_8 = (ctx != 0) ? 7 : 6;
    q->field_C = 0;
    cur = cur + 0x10;
    farFogPlane = cur;
    a2 = skyTex1_1 & 0xFFFFF000FFE7FDE3ULL;
    skyTex1_1 = a2;
    v = (unsigned long long)ext->field_16 | ((unsigned long long)(ext->field_14 & 0xFFF) << 32) |
        ((unsigned long long)((ext->field_14 >> 12) & 3) << 19);
    a2 = a2 | v;
    q = (UnkStruct_texRestoreQword *)cur;
    skyTex1_1 = a2;
    q->field_0 = (unsigned int)a2;
    q->field_4 = (unsigned int)(a2 >> 32);
    q->field_8 = (ctx != 0) ? 0x15 : 0x14;
    q->field_C = 0;
    farFogPlane = cur + 0x10;
    word = ext->field_8 & 0x3FFF;
    tbp = (unsigned long long)word | ((unsigned long long)word << 20) | ((unsigned long long)word << 40);
    q = (UnkStruct_texRestoreQword *)(cur + 0x10);
    if (skyTexCache.field_0 != 0) {
        a2 = ((unsigned long long)ext->field_18 | ((unsigned long long)ext->field_1C << 32)) + tbp;
        q->field_0 = (unsigned int)a2;
        q->field_4 = (unsigned int)(a2 >> 32);
        q->field_8 = (ctx != 0) ? 0x35 : 0x34;
        q->field_C = 0;
        farFogPlane = cur + 0x20;
        a2 = ((unsigned long long)ext->field_20 | ((unsigned long long)ext->field_24 << 32)) + tbp;
    } else {
        a2 = (unsigned long long)ext->field_18 | ((unsigned long long)ext->field_1C << 32);
        q->field_0 = (unsigned int)a2;
        q->field_4 = (unsigned int)(a2 >> 32);
        q->field_8 = (ctx != 0) ? 0x35 : 0x34;
        q->field_C = 0;
        farFogPlane = cur + 0x20;
        a2 = (unsigned long long)ext->field_20 | ((unsigned long long)ext->field_24 << 32);
    }
    q = (UnkStruct_texRestoreQword *)(cur + 0x20);
    q->field_0 = (unsigned int)a2;
    q->field_4 = (unsigned int)(a2 >> 32);
    q->field_8 = (ctx != 0) ? 0x37 : 0x36;
    q->field_C = 0;
    farFogPlane = cur + 0x30;
}

// ---------------------------------------------------------------------------------------------------------------
// boot2 additions (track rw): RpSkyTexCacheAccessRaster, RpSkyTexCacheRasterLock, func_0020E328.

// [C] the raster as these functions read it ([L] RwRaster: +0 parent, +4 cpPixels, +0x20 cType)
struct UnkStruct_texAccRaster {
    void *field_0;                      // [C] parent (the raster itself for a top-level raster) ([L] parent)
    unsigned int field_4;               // [C] tested for 0 ([L] cpPixels)
    unsigned char pad_8[0x18];
    unsigned char field_20;             // [C] bit 0x80 tested, bits 0-2 the type ([L] cType)
};
// [C] the driver's raster extension at raster + skyRasterExt (the offsets used here)
struct UnkStruct_texAccExt {
    unsigned char pad_0[0x8];
    unsigned int field_8;               // [C] TEX0 low (TBP0 bits 0-13)
    unsigned int field_C;               // [C] TEX0 high (CBP bits 5-18)
    unsigned int field_10;              // [C] the palette's offset from the texels
    unsigned short field_14;            // [C] TEX1 K / L
    unsigned char field_16;             // [C] TEX1 low byte
    unsigned char field_17;             // [C] nonzero: locked in the cache
    unsigned int field_18;              // [C] MIPTBP1 low
    unsigned int field_1C;              // [C] MIPTBP1 high
    unsigned int field_20;              // [C] MIPTBP2 low
    unsigned int field_24;              // [C] MIPTBP2 high
    unsigned char pad_28[0x8];
    unsigned int field_30;              // [C] texture cache size in GS words ([L] nTexCacheSize)
    unsigned char pad_34[0x24];
    UnkStruct_skyTexCacheEntry *field_58;   // [C] the cache entry holding the raster, or 0
};
struct UnkStruct_texAccQword {
    unsigned long long field_0;
    unsigned long long field_8;
};
extern unsigned int _rwDMAPktPtr;                 // [C] gp 0x004E27C8
extern "C" int _rwDMAOpenVIFPkt(unsigned int type, unsigned int nQW);
extern "C" void SyncDCache(void *start, void *end);

// [C] sq {lo, hi} at farFogPlane, then farFogPlane += 0x10
static inline void texAccPut(unsigned long long lo, unsigned long long hi)
{
    UnkStruct_texAccQword *q = (UnkStruct_texAccQword *)farFogPlane;

    q->field_0 = lo;
    q->field_8 = hi;
    farFogPlane = farFogPlane + 0x10;
}

// [C] makes a raster the cache's current one. Steps: (1) a raster that is not the current one (field_24), is allocated
// (cType bit 0x80 clear), has pixels and no slot (ext +0x58) gets the upload callback field_20(raster, 0); (2) when
// it has a slot and the cache is set up, TBP0 = slot address >> 6 and CBP = (that + ext +0x10) & 0x3FFF; (3) a raster
// with no slot that is not of type 2 returns 0; (4) the same raster, valid skyTextureRaster and same context (a1)
// returns 1 at once unless it is of type 2 with a parent other than itself; (5) otherwise field_24 = raster and field_28
// = a1, the slot's index (by the 0xAAAAAAAB reciprocal) goes to field_2C (a1 == 0) or field_30 (cache set up), a game
// pointer (_rwDMAPktPtr) is cleared when the raster is unlocked, no async packet (D_004E1904 == 0) and the cache is set
// up, and a VIF packet of 6 qwords (type bit 31) carries {GIFtag NLOOP 5 (0x8005) EOP + 0x10000000 << 32, A+D}, a
// TEXFLUSH (0x3F), TEX0 (as RpSkyTexCacheRestore; type 2 adds the flip data word), then a1 == 0: TEX1 (K / L from ext
// +0x14 / +0x16, skyTex1_1 updated), else a 0x7F register write of 0; then MIPTBP1 / MIPTBP2 (0x34 / 0x36, or 0x35 /
// 0x37 for a1 != 0) with the replicated TBP0 added when the cache is set up. D_004E1904 = 0. Returns 1.
// 0x0020D1E8
extern "C" int RpSkyTexCacheAccessRaster(void *raster, int ctx)
{
    UnkStruct_texAccRaster *r = (UnkStruct_texAccRaster *)raster;
    UnkStruct_texAccExt *x = (UnkStruct_texAccExt *)((char *)raster + skyRasterExt);
    UnkStruct_skyTexCacheEntry *e;
    unsigned long long a2;
    unsigned long long v;
    unsigned long long tbp;
    unsigned int t;
    unsigned int type;
    unsigned int word;

    if (raster != (void *)skyTexCache.field_24) {
        if ((r->field_20 & 0x80) == 0 && r->field_4 != 0 && x->field_58 == 0) {
            ((void (*)(void *, int))skyTexCache.field_20)(raster, 0);
        }
    }
    e = x->field_58;
    if (e != 0 && skyTexCache.field_0 != 0) {
        t = e->field_4 >> 6;
        x->field_8 = (x->field_8 & 0xFFFFC000) | (t & 0x3FFF);
        x->field_C = (x->field_C & 0xFFF8001F) | (((t + x->field_10) & 0x3FFF) << 5);
    }
    // [C 0x20D2B4] the type test; the slot is re-read from the extension
    type = r->field_20 & 7;
    if (type != 2 && x->field_58 == 0) {
        return 0;
    }
    if (raster == (void *)skyTexCache.field_24 && skyTextureRaster != 0xFFFFFFFFu && skyTexCache.field_28 == ctx) {
        if (type != 2) {
            return 1;
        }
        if (r->field_0 == raster) {
            return 1;
        }
    }
    skyTexCache.field_24 = (int)(long)raster;
    skyTexCache.field_28 = ctx;
    e = x->field_58;
    if (e != 0 && skyTexCache.field_0 != 0) {
        t = (unsigned int)((int)((unsigned int)((char *)e - (char *)skyTexCache.field_14) * 0xAAAAAAABu) >> 2);
        if (ctx == 0) {
            skyTexCache.field_2C = (int)t;
        } else {
            skyTexCache.field_30 = (int)t;
        }
    }
    if (x->field_17 == 0 && D_004E1904 == 0 && skyTexCache.field_0 != 0) {
        _rwDMAPktPtr = 0;
    }
    _rwDMAOpenVIFPkt(0x80000000, 6);
    texAccPut(0x1000000000008005ULL, 0xE);
    texAccPut(0, 0x3F);
    a2 = (unsigned long long)x->field_8 | ((unsigned long long)x->field_C << 32);
    if (type == 2) {
        if ((skyFrameBit & 1) == 0) {
            v = _rwDMAFlipData.q[0x60 / 8];
        } else {
            v = _rwDMAFlipData.q[0x1D0 / 8];
        }
        word = x->field_8;
        a2 = (a2 & 0xFFFFFFFFFFFFC000ULL) | (unsigned long long)((word & 0x3FFF) + ((v & 0x1FF) << 5));
    }
    texAccPut(a2, (ctx != 0) ? 7 : 6);
    if (ctx != 0) {
        texAccPut(0, 0x7F);
    } else {
        a2 = skyTex1_1 & 0xFFFFF000FFE7FDE3ULL;
        skyTex1_1 = a2;
        v = (unsigned long long)x->field_16 | ((unsigned long long)(x->field_14 & 0xFFF) << 32) |
            ((unsigned long long)((x->field_14 >> 12) & 3) << 19);
        a2 = a2 | v;
        skyTex1_1 = a2;
        texAccPut(a2, 0x14);
    }
    word = x->field_8 & 0x3FFF;
    tbp = (unsigned long long)word | ((unsigned long long)word << 20) | ((unsigned long long)word << 40);
    if (skyTexCache.field_0 != 0) {
        a2 = ((unsigned long long)x->field_18 | ((unsigned long long)x->field_1C << 32)) + tbp;
        texAccPut(a2, (ctx != 0) ? 0x35 : 0x34);
        a2 = ((unsigned long long)x->field_20 | ((unsigned long long)x->field_24 << 32)) + tbp;
        texAccPut(a2, (ctx != 0) ? 0x37 : 0x36);
    } else {
        a2 = (unsigned long long)x->field_18 | ((unsigned long long)x->field_1C << 32);
        texAccPut(a2, (ctx != 0) ? 0x35 : 0x34);
        a2 = (unsigned long long)x->field_20 | ((unsigned long long)x->field_24 << 32);
        texAccPut(a2, (ctx != 0) ? 0x37 : 0x36);
    }
    D_004E1904 = 0;
    return 1;
}

// [C] locks (a1 != 0) a raster into the cache's upper part, or unlocks it (a1 == 0). The extension is the PARENT's
// (raster +0) at skyRasterExt; the cache is checked first (field_0 == 0 -> returns 0).
// Lock: already locked (ext +0x17) -> 1; fewer than 7 entries below the top (field_10 < 7) -> 0; the top entry's
// address minus field_18 below the raster's size (ext +0x30) -> 0. Else the ring field_8 .. field_4 (when field_4 !=
// -1; wrapping at field_10) is emptied (each entry's raster loses ext +0x58, field_24 cleared when it is that raster,
// the entry's raster word cleared) and the ring reset to -1 / -1; entry field_10 - 1 gets the raster, the address
// (field_18 + field_1C when field_10 equals the entry count D_004E1900, else the next entry's address) minus the size,
// and the size at +8; field_10 decremented, ext +0x58 = the entry, ext +0x17 = 1; unless the raster is not allocated
// (cType bit 0x80) it is uploaded with skyTexCacheMemRasterToGs(raster, address, 0) (only with pixels); TBP0 / CBP
// from the entry's address as in AccessRaster. Returns 1.
// Unlock: not locked -> 1; else ext +0x17 = 0, the entry's raster word and ext +0x58 cleared, field_24 cleared when it
// is this raster; when the entry's index equals field_10 and is below the entry count, field_10 advances over empty
// entries (stopping at a filled one or the count). Returns 1.
// 0x0020D7E8
extern "C" int RpSkyTexCacheRasterLock(void *raster, int flag)
{
    UnkStruct_texAccRaster *r = (UnkStruct_texAccRaster *)raster;
    UnkStruct_texAccExt *x;
    UnkStruct_skyTexCacheEntry *entries;
    UnkStruct_skyTexCacheEntry *ent;
    unsigned int top;
    unsigned int i;
    unsigned int end;
    unsigned int wrap;
    unsigned int cnt;
    unsigned int size;
    unsigned int v;
    unsigned int t;
    int extOff;
    void *er;

    extOff = skyRasterExt;
    x = (UnkStruct_texAccExt *)((char *)r->field_0 + extOff);
    if (skyTexCache.field_0 == 0) {
        return 0;
    }
    if (flag == 0) {
        // [C 0x20D9F8] unlock
        if (x->field_17 == 0) {
            return 1;
        }
        x->field_17 = 0;
        ent = x->field_58;
        ent->field_0 = 0;
        x->field_58 = 0;
        i = (unsigned int)((int)((unsigned int)((char *)ent - (char *)skyTexCache.field_14) * 0xAAAAAAABu) >> 2);
        if (skyTexCache.field_24 == (int)(long)raster) {
            skyTexCache.field_24 = 0;
        }
        if (i != skyTexCache.field_10) {
            return 1;
        }
        cnt = D_004E1900;
        if (!(i < cnt)) {
            return 1;
        }
        if (skyTexCache.field_14[(int)i].field_0 != 0) {
            return 1;
        }
        for (;;) {
            v = skyTexCache.field_10 + 1;
            skyTexCache.field_10 = v;
            if (!(v < cnt)) {
                break;
            }
            if (skyTexCache.field_14[(int)v].field_0 != 0) {
                break;
            }
        }
        return 1;
    }
    if (x->field_17 != 0) {
        return 1;
    }
    top = skyTexCache.field_10;
    if (top < 7) {
        return 0;
    }
    size = x->field_30;
    if (skyTexCache.field_14[(int)top].field_4 - (unsigned int)skyTexCache.field_18 < size) {
        return 0;
    }
    if (skyTexCache.field_4 != 0xFFFFFFFF) {
        // [C 0x20D86C] empty the ring field_8 .. field_4
        wrap = top;
        i = skyTexCache.field_8;
        for (;;) {
            if (i == wrap) {
                i = 0;
            }
            er = skyTexCache.field_14[(int)i].field_0;
            if (er != 0) {
                *(unsigned int *)((char *)er + extOff + 0x58) = 0;
                if (skyTexCache.field_14[(int)i].field_0 == (void *)skyTexCache.field_24) {
                    skyTexCache.field_24 = 0;
                }
                skyTexCache.field_14[(int)i].field_0 = 0;
            }
            if (i == skyTexCache.field_4) {
                break;
            }
            i = i + 1;
        }
    }
    // [C 0x20D8E4]
    top = skyTexCache.field_10;
    entries = skyTexCache.field_14;
    skyTexCache.field_8 = 0xFFFFFFFF;
    skyTexCache.field_4 = 0xFFFFFFFF;
    cnt = D_004E1900;
    size = x->field_30;
    ent = &entries[(int)(top - 1)];
    ent->field_0 = raster;
    if (top == cnt) {
        v = (unsigned int)skyTexCache.field_18 + (unsigned int)skyTexCache.field_1C - size;
    } else {
        v = entries[(int)top].field_4 - size;
    }
    ent->field_4 = v;
    ent = &skyTexCache.field_14[(int)(top - 1)];
    *(unsigned int *)((char *)ent + 8) = x->field_30;
    skyTexCache.field_10 = top - 1;
    x->field_58 = ent;
    x->field_17 = 1;
    if ((r->field_20 & 0x80) == 0 && r->field_4 != 0) {
        skyTexCacheMemRasterToGs(raster, skyTexCache.field_14[(int)(top - 1)].field_4, 0);
    }
    t = x->field_58->field_4 >> 6;
    x->field_8 = (x->field_8 & 0xFFFFC000) | (t & 0x3FFF);
    x->field_C = (x->field_C & 0xFFF8001F) | (((t + x->field_10) & 0x3FFF) << 5);
    return 1;
}

// [C] (dst, src, byteCount, unused) -> dst. Builds a DMA chain in place: dst rounded up to 64 bytes; n = (count + 15)
// >> 4 qwords (the asm: daddiu 0xF, dsll 28, dsra32), chunked in 0x7FEE qwords: per chunk the qword {0x10000001,
// 0x5000000100000000} (DMA/VIF tag template), a GIFtag {NLOOP = chunk, FLG 2 (IMAGE)} with high 0, and a VIF qword
// {chunk | 0x30000000, src (byte address) << 32 ... high (chunk | 0x50000000) << 32}; src advances by 0x7FEE0 per
// chunk. Chunk count = (n + 0x7FED) / 0x7FEE (signed; break 7 never taken, the divisor is constant). Then {0x60000002,
// 0x5000000200000000}, {0x1000000000008001, 0xE} and the first qword of `state` (the TEXFLUSH / TRXPOS template, see
// skyTexCacheOpenCache); SyncDCache(rounded dst, last qword's address + 0x8F). [L] the VIF/DMA meanings.
// 0x0020E328
extern "C" void *func_0020E328(void *dst, unsigned int src, int count, int unused)
{
    unsigned int dstv = (unsigned int)(long)dst;
    long long x = (long long)count + 15;
    int t3 = (int)((x << 28) >> 32);
    int t0 = t3 + 0x7FED;
    int t4 = t0 / 0x7FEE;
    unsigned int t2 = (dstv + 0x3F) & 0xFFFFFFC0u;
    unsigned int cur;
    int i;
    int chunk;
    UnkStruct_texAccQword *q;

    (void)unused;
    cur = t2;
    if (t4 > 0) {
        for (i = 0; i < t4; i++) {
            q = (UnkStruct_texAccQword *)cur;
            q->field_0 = 0x10000001ULL;
            q->field_8 = 0x5000000100000000ULL;
            t2 = t2 + 0x10;
            cur = cur + 0x10;
            chunk = (t3 < 0x7FEF) ? t3 : 0x7FEE;
            q = (UnkStruct_texAccQword *)cur;
            q->field_0 = (unsigned long long)(unsigned int)chunk | 0x800000000000000ULL;
            q->field_8 = 0;
            cur = cur + 0x10;
            q = (UnkStruct_texAccQword *)cur;
            q->field_0 = (unsigned long long)(unsigned int)(chunk | 0x30000000) | ((unsigned long long)src << 32);
            q->field_8 = (unsigned long long)(unsigned int)(chunk | 0x50000000) << 32;
            t2 = t2 + 0x20;
            cur = cur + 0x10;
            src = src + 0x7FEE0;
            t3 = t3 - 0x7FEE;
        }
    }
    q = (UnkStruct_texAccQword *)t2;
    q->field_0 = 0x60000002ULL;
    q->field_8 = 0x5000000200000000ULL;
    t2 = t2 + 0x10;
    q = (UnkStruct_texAccQword *)t2;
    q->field_0 = 0x1000000000008001ULL;
    q->field_8 = 0xE;
    t2 = t2 + 0x10;
    q = (UnkStruct_texAccQword *)t2;
    q->field_0 = state.field_0;
    q->field_8 = state.field_8;
    SyncDCache((void *)(long)((dstv + 0x3F) & 0xFFFFFFC0u), (void *)(long)(t2 + 0x8F));
    return dst;
}
