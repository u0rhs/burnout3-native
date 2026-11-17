// RenderWare 3.6 sky2 driver: raster create / destroy and their helpers (RW 3.1 file name: driver/sky2/basky.c; reading
// aid only, 3.6 differs). Written from the retail asm (decomp assembly/asm/rw/renderware.s). Tags: [C] confirmed from
// the asm, [L], [G]. boot2. The structs below are this file's own views (other files declare other views of the same
// objects under the same names).
//
// Locals that retail keeps in its stack frame (the RwError pair, the per-level arrays) are host locals here: they are
// not guest-visible after the return, so their exact stack slots are not reproduced.

// [C] RwRaster: 0x34 bytes ([L] RW 3.x RwRaster: parent, cpPixels, palette, width, height, depth, stride, nOffsetX/Y,
// cType, cFlags, privateFlags, cFormat, originalPixels, originalWidth, originalHeight, originalStride).
struct UnkStruct_RwRaster {
    void *field_0;                  // [L] parent
    unsigned char *field_4;         // [C] the pixels (malloc'd; for new-style rasters first the packet header) ([L] cpPixels)
    unsigned char *field_8;         // [C] the palette ([L] palette)
    int field_C;                    // [C] width
    int field_10;                   // [C] height
    int field_14;                   // [C] depth (bits per pixel)
    int field_18;                   // [C] stride (bytes per row)
    short field_1C;
    short field_1E;
    unsigned char field_20;         // [C] the type: flags & 7 (0 normal, 1 Z buffer, 2 camera, 4 texture, 5 camera texture)
    unsigned char field_21;         // [C] flags & 0xF8 (0x80: no allocation [L rwRASTERDONTALLOCATE]; 0x40 cleared here)
    unsigned char field_22;
    unsigned char field_23;         // [C] the format >> 8 (0x60 palette bits, 0x80 mipmap, low nibble the pixel format)
    unsigned char *field_24;        // [C] = field_4 ([L] originalPixels)
    int field_28;                   // [C] = field_C ([L] originalWidth)
    int field_2C;                   // [C] = field_10 ([L] originalHeight)
    int field_30;                   // [C] = field_18 ([L] originalStride)
};

// [C] the driver's raster extension at raster + skyRasterExt (the offsets this file uses; texcache.cpp: +0x58, +0x17)
// ([L] 3.1 _SkyRasterExt; meanings from 3.1's SkyRasterCreate, which fills the same slots).
struct UnkStruct_skyRasterExt {
    int field_0;                    // [C] waited down to 0 by SkyRasterDestroy (forced buffer swaps) ([L] DMA references)
    int field_4;                    // [C] cleared at the end of SkyRasterCreate
    unsigned int field_8;           // [C] TEX0 low word: TBW << 14, PSM << 20, TW << 26, TH low bits << 30 ([L] lsb)
    unsigned int field_C;           // [C] TEX0 high word: TH >> 2, TCC bit 2, CPSM << 19, CLD bit 29 ([L] msb)
    unsigned int field_10;          // [C] the palette's block address ([L] palOffset)
    unsigned short field_14;        // [C] skyDefaultMipmapKL ([L] mipmapKL)
    unsigned char field_16;         // [C] (levels - 1) << 2 ([L] maxMipLevel)
    unsigned char field_17;         // [C] nonzero: SkyRasterDestroy unlocks first ([L] bLocked)
    unsigned int field_18;          // [C] MIPTBP1 low word
    unsigned int field_1C;          // [C] MIPTBP1 high word
    unsigned int field_20;          // [C] MIPTBP2 low word
    unsigned int field_24;          // [C] MIPTBP2 high word
    int field_28;                   // [C] system memory size of the pixels (new-style: the packets' size) ([L] sysMemSize)
    int field_2C;                   // [C] palette size in bytes (new-style: + its packet) ([L] sysMemPalSize)
    int field_30;                   // [C] texture cache size (VRAM bytes, page aligned) ([L] nTexCacheSize)
    unsigned char field_34;         // [C] 1 for textures ([L] cachePkts)
    unsigned char pad_35;
    unsigned char field_36;         // [C] 1 new-style raster, 2 twiddled PSMT8, 4 twiddled PSMT4 ([L] flags)
    unsigned char pad_37;
    void *field_38;                 // [C] freed by SkyRasterDestroy ([L] palUploadPkt)
    void *field_3C[7];              // [C] freed by SkyRasterDestroy ([L] mipUploadPkts)
    void *field_58;                 // [C] cleared here ([L] mpCacheEntry; texcache.cpp clears it too)
};

// [C] one 128-bit slot of a packet (sq of a pcpyld pair: low doubleword at +0, high at +8)
struct UnkStruct_skyQword {
    unsigned long long field_0;
    unsigned long long field_8;
};

// [C] customMode (0x00675F10): the words SkyGetRasterFormat compares (basky.cpp: two RwVideoMode-like halves).
struct UnkStruct_customMode {
    unsigned char pad_0[0x8];
    int field_8;                    // [C] draw depth
    unsigned char pad_C[0x8];
    int field_14;                   // [C] draw raster format
    unsigned char pad_18[0x8];
    int field_20;                   // [C] display depth
    unsigned char pad_24[0x8];
    int field_2C;                   // [C] display raster format
    unsigned char pad_30[0x4];
};

// [C] *skyVideoMode: width, height, depth (an RwVideoMode at its start [L])
struct UnkStruct_skyVideoMode {
    int field_0;                    // [C] width
    int field_4;                    // [C] height
    int field_8;                    // [C] depth
};

struct UnkStruct_ourGlobals {
    unsigned char pad_0[0x134];
    void *(*field_134)(unsigned int size, unsigned int hint);   // [C] the malloc function ([L] memoryFuncs.rwmalloc)
    void (*field_138)(void *);              // [C] the free function ([L] memoryFuncs.rwfree)
};

// [C] RwError on the stack: {plugin ID 1 ([L] rwID_COREPLUGIN), _rwerror(code)} ([L] RwError).
struct UnkStruct_RwErrorSet {
    int field_0;
    int field_4;
};

extern UnkStruct_ourGlobals ourGlobals;         // [C] 0x00670BD0
extern UnkStruct_customMode customMode;         // [C] 0x00675F10
extern UnkStruct_skyVideoMode *skyVideoMode;    // [C] gp 0x004E1808
extern int skyRasterExt;                        // [C] gp 0x004E2858: the raster plugin's offset
extern int currentCameraFormat;                 // [C] gp-relative (0x100 / 0x500)
extern int zBufferDepth;                        // [C] gp-relative (16 / 32)
extern int skyMaxMipLevels;                     // [C] gp 0x004E1888
extern unsigned int skyDefaultMipmapKL;         // [C] gp 0x004E18A0 (u32; the asm reads its low half: lhu)
extern int skyNoNewStyleRasters;                // [C] gp 0x004E1818
extern unsigned char bz5120;                    // [C] gp 0x004E1814 (u8): nonzero lets a camera raster exceed the mode
// [C] function-static tables of SkyRasterCreate / skyPrepareUploadRaster; their table names (rasterPageWidths.171 ...)
// are not C identifiers.
extern unsigned int D_004B89B0[];               // [C] rasterPageWidths.171, 32 entries by depth - 1
extern unsigned int D_004B8A30[];               // [C] rasterPageHeights.172, 32 entries by depth - 1
extern unsigned int D_004B8AB0[];               // [C] PSCMT3224BlockOffsets.173, 8 entries
extern unsigned int D_004B8AD0[];               // [C] PSCMT16BlockOffsets.174
extern unsigned int D_004B8AF0[];               // [C] PSCMT16SBlockOffsets.175
extern unsigned int D_004B8B10[];               // [C] PSMT8BlockOffsets.176
extern unsigned int D_004B8B30[];               // [C] PSMT4BlockOffsets.177
extern unsigned int D_004B94D8[];               // [C] palWidths.18, 8 entries by depth - 1
extern unsigned int D_004B94F8[];               // [C] palHeights.19, 8 entries by depth - 1

// [C] _rwerror (0x001D89B8) only returns its first argument (badevice_more.cpp); retail passes a second one (the size)
// with 0x80000013, never read.
extern "C" int _rwerror(int code);
extern "C" void *RwErrorSet(UnkStruct_RwErrorSet *code);
extern "C" void SyncDCache(void *start, void *end);
extern "C" int _rwDMAForceBufferSwap(void);                      // [C] badma.cpp
extern "C" int RpSkyTexCacheRasterLock(void *raster, int flag);  // [C] called with 0; result not read
extern "C" void RpSkyTexCacheReleaseRaster(void *raster);         // [C] result not read
// [C] (width, height, pixels, depth or palette bits, cachePkts) -> the upload packet; a0..a3, t0
extern "C" void *_SkyBuildPktForUpLoadAlignedContiguousRectangle(int w, int h, void *pixels, int depth, int cache);
extern "C" int SkyGetRasterFormat(unsigned int pInOut, UnkStruct_RwRaster *raster, int flags);
extern "C" void calcOffsets(unsigned int width, unsigned int height, int format, unsigned long long *offsets,
                            unsigned long long *bufwidth, unsigned int *xyoff, unsigned int *texMemSize,
                            unsigned int *paletteOffset);
extern "C" void skyPrepareUploadRaster(UnkStruct_RwRaster *raster);

// [C] the error pattern every failure repeats: {1, _rwerror(code)} on the stack, RwErrorSet(&it)
static inline void func_001F6588_inl1(int code)
{
    UnkStruct_RwErrorSet err;

    err.field_0 = 1;
    err.field_4 = _rwerror(code);
    RwErrorSet(&err);
}

// [C] the raster's extension
static inline UnkStruct_skyRasterExt *func_001F6588_inl2(UnkStruct_RwRaster *raster)
{
    return (UnkStruct_skyRasterExt *)((char *)raster + skyRasterExt);
}

// [C] the type (flags & 7) and the flags (flags & 0xF8) are stored first; then by type (jtbl_004B8640, 6 entries; type
// 3 and >= 6 return 0 without an error): the depth and the format byte +0x23 = (pixel format | mipmap | palette) >> 8,
// checked against the depth / the video mode / the Z depth. Errors: 0x8000000D (format), 0x8000000C (depth); 0 then.
// a0 (pInOut) is not read.
// 0x001F6588
extern "C" int SkyGetRasterFormat(unsigned int pInOut, UnkStruct_RwRaster *raster, int flags)
{
    int type = flags & 7;
    int mip = flags & 0x9000;       // [C] t0 ([L] rwRASTERFORMATMIPMAP 0x8000 | AUTOMIPMAP 0x1000)
    int fmt = flags & 0xF00;        // [C] a3 ([L] the pixel format)
    int pal = flags & 0x6000;       // [C] a2 ([L] PAL8 0x2000 / PAL4 0x4000)
    int depth;
    int cur;

    raster->field_20 = (unsigned char)type;
    raster->field_21 = (unsigned char)(flags & 0xF8);
    switch (type) {
    case 0:
        // [C 0x1F65D4] normal: no palette, no mipmap
        if (pal != 0 || mip != 0) {
            func_001F6588_inl1(0x8000000D);
            return 0;
        }
        // [C] then the camera-texture check (0x1F6604) and the texture path
        if (pal != 0) {
            func_001F6588_inl1(0x8000000D);
            return 0;
        }
        goto texture;
    case 5:
        // [C 0x1F6604] camera texture: no palette
        if (pal != 0) {
            func_001F6588_inl1(0x8000000D);
            return 0;
        }
        goto texture;
    case 4:
        goto texture;
    case 2:
        // [C 0x1F68E4] camera: no mipmap, no palette; the depth one of the mode's (0: the video mode's), the format 0
        // (the current camera format), the current one or one of the mode's, and matching the depth
        if (mip != 0 || pal != 0) {
            func_001F6588_inl1(0x8000000D);
            return 0;
        }
        depth = raster->field_14;
        if (depth == 0) {
            raster->field_14 = skyVideoMode->field_8;
        } else if (depth != customMode.field_8 && depth != customMode.field_20) {
            func_001F6588_inl1(0x8000000C);
            return 0;
        }
        cur = currentCameraFormat;
        if (fmt != 0 && fmt != cur && fmt != customMode.field_14 && fmt != customMode.field_2C) {
            func_001F6588_inl1(0x8000000D);
            return 0;
        }
        if (fmt == 0) {
            fmt = cur;
        }
        if (raster->field_14 == 0x20) {
            if (fmt != 0x500) {
                func_001F6588_inl1(0x8000000D);
                return 0;
            }
        } else if (fmt != 0x100) {
            func_001F6588_inl1(0x8000000D);
            return 0;
        }
        raster->field_23 = (unsigned char)(fmt >> 8);
        return 1;
    case 1:
        // [C 0x1F6A24] Z buffer: no mipmap, no palette; the depth 0 or zBufferDepth (stored), the format 0 or 0x700
        // (16-bit Z) / 0x900 (32-bit Z); the format byte from zBufferDepth (re-read)
        if (mip != 0 || pal != 0) {
            func_001F6588_inl1(0x8000000D);
            return 0;
        }
        depth = raster->field_14;
        cur = zBufferDepth;
        if (depth != 0 && depth != cur) {
            func_001F6588_inl1(0x8000000C);
            return 0;
        }
        raster->field_14 = cur;
        if (fmt != 0) {
            if (cur == 0x10) {
                if (fmt != 0x700) {
                    func_001F6588_inl1(0x8000000D);
                    return 0;
                }
            } else if (fmt != 0x900) {
                func_001F6588_inl1(0x8000000D);
                return 0;
            }
        }
        fmt = (zBufferDepth == 0x10) ? 0x700 : 0x900;
        raster->field_23 = (unsigned char)(fmt >> 8);
        return 1;
    default:
        // [C 0x1F6AD0] type 3 and >= 6
        return 0;
    }

texture:
    // [C 0x1F6628] no pixel format: from the depth (jtbl_004B8660 over depth - 4: 4 -> 0x100 + PAL4, 8 -> 0x100 + PAL8,
    // 24 -> 0x600, 32 -> 0x500, anything else 0x100)
    if (fmt == 0) {
        switch (raster->field_14) {
        case 4:
            fmt = 0x100;
            pal = 0x4000;
            break;
        case 8:
            fmt = 0x100;
            pal = 0x2000;
            break;
        case 0x18:
            fmt = 0x600;
            break;
        case 0x20:
            fmt = 0x500;
            break;
        default:
            fmt = 0x100;
            break;
        }
    }
    raster->field_23 = (unsigned char)((fmt | mip | pal) >> 8);
    if (pal == 0x2000) {
        // [C 0x1F6858] PAL8: depth 0 or 8 (stored 8), pixel format 0x100 or 0x500
        depth = raster->field_14;
        if (depth != 0 && depth != 8) {
            func_001F6588_inl1(0x8000000C);
            return 0;
        }
        raster->field_14 = 8;
        if ((fmt & 0xF00) == 0x100 || (fmt & 0xF00) == 0x500) {
            return 1;
        }
        func_001F6588_inl1(0x8000000D);
        return 0;
    }
    if (pal == 0x4000) {
        // [C 0x1F67E4] PAL4: depth 0 or 4 (stored 4), pixel format 0x100 or 0x500
        depth = raster->field_14;
        if (depth != 0 && depth != 4) {
            func_001F6588_inl1(0x8000000C);
            return 0;
        }
        raster->field_14 = 4;
        if ((fmt & 0xF00) == 0x100 || (fmt & 0xF00) == 0x500) {
            return 1;
        }
        func_001F6588_inl1(0x8000000D);
        return 0;
    }
    if (pal != 0) {
        // [C 0x1F68C8] both palette bits
        func_001F6588_inl1(0x8000000D);
        return 0;
    }
    // [C 0x1F66CC] no palette: 0x100 -> depth 16, 0x600 -> 24, 0x500 -> 32 (0 or that depth, stored); else 0x8000000D
    if (fmt == 0x500) {
        depth = raster->field_14;
        if (depth != 0 && depth != 0x20) {
            func_001F6588_inl1(0x8000000C);
            return 0;
        }
        raster->field_14 = 0x20;
        return 1;
    }
    if (fmt == 0x100) {
        depth = raster->field_14;
        if (depth != 0 && depth != 0x10) {
            func_001F6588_inl1(0x8000000C);
            return 0;
        }
        raster->field_14 = 0x10;
        return 1;
    }
    if (fmt == 0x600) {
        depth = raster->field_14;
        if (depth != 0 && depth != 0x18) {
            func_001F6588_inl1(0x8000000C);
            return 0;
        }
        raster->field_14 = 0x18;
        return 1;
    }
    func_001F6588_inl1(0x8000000D);
    return 0;
}

// [C] the GS block number -> block address swizzles of calcOffsets (on the low 32 bits; the asm's 64-bit and / or
// results are then sign-extended from bit 31, which only the low word survives)
static inline unsigned int func_001F6B08_inl1(unsigned int v)
{
    // [C 0x1F792C / 0x1F7BF8] PSMCT32 / 24, PSMT8 (H), PSMT4HL / HH
    return (v & ~0x1Eu) | ((v & 2) << 1) | ((v & 4) << 2) | ((v & 8) >> 2) | ((v & 0x10) >> 1);
}

static inline unsigned int func_001F6B08_inl2(unsigned int v)
{
    // [C 0x1F797C / 0x1F7C34] PSMCT16, PSMT4
    return (v & ~0x1Fu) | ((v & 1) << 1) | ((v & 2) << 2) | ((v & 4) >> 2) | ((v & 8) >> 1) | (v & 0x10);
}

static inline unsigned int func_001F6B08_inl3(unsigned int v)
{
    // [C 0x1F78C0 / 0x1F7BB4] PSMCT16S
    return (v & ~0x1Fu) | ((v & 1) << 1) | ((v & 2) << 3) | ((v & 4) >> 2) | (v & 8) | ((v & 0x10) >> 2);
}

// [C] by format (jtbl_004B87D0 for the levels, jtbl_004B88C0 for the palette, the same mapping): the Z formats
// (0x30..0x32, 0x3A) are their colour swizzle ^ 0x18; anything else (or format >= 0x3B) unchanged.
static inline unsigned int func_001F6B08_inl4(int format, unsigned int v)
{
    switch (format) {
    case 0x00:
    case 0x01:
    case 0x13:
    case 0x1B:
    case 0x24:
    case 0x2C:
        return func_001F6B08_inl1(v);
    case 0x02:
    case 0x14:
        return func_001F6B08_inl2(v);
    case 0x0A:
        return func_001F6B08_inl3(v);
    case 0x30:
    case 0x31:
        return func_001F6B08_inl1(v) ^ 0x18;
    case 0x32:
        return func_001F6B08_inl2(v) ^ 0x18;
    case 0x3A:
        return func_001F6B08_inl3(v) ^ 0x18;
    default:
        return v;
    }
}

// [C] the mipmap layout (3.1's BETTERPACK calcOffsets, with 3.6's changes): block / page sizes by format
// (jtbl_004B86E0), the level count (min(width, height) halved while > 8, up to skyMaxMipLevels), per level the block
// offset, buffer width (in 64 pixels) and start, packing small levels beside / below the previous ones with a stack of
// free rectangles; then the palette's block, the TRXPOS offsets (xyoff), the tiled-offset fixup, the block -> address
// swizzle, *paletteOffset and *texMemSize (words). 64-bit arithmetic where retail calls __muldi3 / __udivdi3 /
// __umoddi3 (plain C operators here: they have no side effects).
// [C] retail differences from 3.1 kept: the tiled fixup multiplies / divides by the ratios (3.1 shifts by ratio - 1);
// the "go down" case adds height when blockHeight != 0 (3.1's `blockHeight ? height : blockHeight`); the pop in the
// width < page, height < page case has no stackp check (index -1 reads the previous array's last word in retail's frame:
// the six local arrays are one block here, in retail's order).
// 0x001F6B08
extern "C" void calcOffsets(unsigned int width, unsigned int height, int format, unsigned long long *offsets,
                            unsigned long long *bufwidth, unsigned int *xyoff, unsigned int *texMemSize,
                            unsigned int *paletteOffset)
{
    unsigned int bW;    // [C] s7 blockWidth
    unsigned int bH;    // [C] t7 blockHeight
    unsigned int pW;    // [C] t6 pageWidth
    unsigned int pH;    // [C] sp+0xD8 pageHeight
    // [C] sp+0x00..0xBF: bufheight[8], bufstart[8], stackx[8], stackh[8], stackw[8], stacko[8] ([G] zeroed here; retail
    // leaves the slots it does not write as they were)
    unsigned int frame[0x30] = {0};
    unsigned int *bufheight = &frame[0x00];
    unsigned int *bufstart = &frame[0x08];
    unsigned int *stackx = &frame[0x10];
    unsigned int *stackh = &frame[0x18];
    unsigned int *stackw = &frame[0x20];
    unsigned int *stacko = &frame[0x28];
    int inTable = (unsigned int)format < 0x3B;      // [C] sp+0x140
    int maxlevel;
    int i;
    int k;
    int m;
    int t;
    int stackp;         // [C] fp
    int piviot;         // [C] sp+0xE8
    unsigned int ix;    // [C] sp+0xE0 interioroffsetx
    unsigned int iy;    // [C] sp+0xE4 interioroffsety
    unsigned int mw;
    unsigned int mh;
    unsigned int hw;
    unsigned int hh;
    unsigned int a2;
    unsigned int a1;
    unsigned int t5;
    unsigned int palette;
    unsigned int bs;
    unsigned int bw;
    unsigned int S1;
    unsigned int S2;
    unsigned int S3;
    unsigned long long d;
    unsigned long long bw64;
    unsigned long long o;

    switch (format) {
    case 0x00:
    case 0x01:
    case 0x1B:
    case 0x24:
    case 0x2C:
        // [C 0x1F6B78] PSMCT32 / 24, PSMT8H, PSMT4HL / HH
        pH = 0x20;
        bW = 8;
        bH = 8;
        pW = 0x40;
        break;
    case 0x13:
        // [C 0x1F6B90] PSMT8
        pH = 0x40;
        bW = 0x10;
        bH = 0x10;
        pW = 0x80;
        break;
    case 0x14:
        // [C 0x1F6BA8] PSMT4
        pW = 0x80;
        bW = 0x20;
        bH = 0x10;
        pH = 0x80;
        break;
    case 0x30:
    case 0x31:
        // [C 0x1F6BBC] PSMZ32 / 24
        pH = 0x20;
        bW = 8;
        bH = 8;
        pW = 0x40;
        break;
    case 0x02:
    case 0x0A:
    case 0x32:
        // [C 0x1F6BD4] PSMCT16 / 16S, PSMZ16
        pW = 0x40;
        bW = 0x10;
        pH = 0x40;
        bH = 8;
        break;
    default:
        // [C 0x1F6BE8] PSMZ16S and anything else
        pW = 0x40;
        bW = 0x10;
        pH = 0x40;
        bH = 8;
        break;
    }

    // [C 0x1F6BF8..0x1F6D3C] an unrolled loop; signed compares (slti / slt)
    maxlevel = 1;
    k = (int)(height > width ? width : height);
    while (k > 8 && maxlevel < skyMaxMipLevels) {
        k >>= 1;
        maxlevel++;
    }

    offsets[0] = 0;
    bufwidth[0] = (unsigned int)(((width + pW - 1) / pW) * (pW >> 6));
    bufheight[0] = (height + pH - 1) / pH;
    bufstart[0] = 0;
    ix = 0;
    iy = 0;
    piviot = 0;
    stackp = 0;
    for (i = 1; i < maxlevel; i++) {
        if (width >= pW) {
            if (height >= pH) {
                // [C 0x1F6E2C] the previous level filled whole pages
                ix = 0;
                iy = 0;
                offsets[i] = offsets[i - 1] + (unsigned int)((width / bW) * (height / bH));
                bufwidth[i] = (unsigned int)((((width >> 1) + pW - 1) / pW) * (pW >> 6));
                bufheight[i] = ((height >> 1) + pH - 1) / pH;
                bufstart[i] = (unsigned int)offsets[i];
            } else {
                // [C 0x1F70AC] whole pages wide, space below
                mh = (bH < height) ? height : bH;
                bufstart[i] = bufstart[i - 1];
                bufwidth[i] = bufwidth[i - 1];
                bufheight[i] = bufheight[i - 1];
                if (iy + mh < pH) {
                    iy = iy + mh;
                    offsets[i] = offsets[i - 1]
                        + (unsigned long long)(unsigned int)(((height + bH - 1) / bH) * (pW / bW))
                        * (bufwidth[i] / (unsigned long long)(pW >> 6));
                } else {
                    // [C 0x1F7160]
                    ix = ix + ((bW < width) ? width : bW);
                    offsets[i] = offsets[i - 1] + (unsigned int)((width + bW - 1) / bW);
                }
            }
        } else if (height >= pH) {
            // [C 0x1F6ECC] a page high, space to the right
            mw = (bW < width) ? width : bW;
            bufstart[i] = bufstart[i - 1];
            bufwidth[i] = bufwidth[i - 1];
            bufheight[i] = bufheight[i - 1];
            if (ix + mw < pW) {
                // [C 0x1F6EFC] push the space below this level
                stackx[stackp] = ix;
                stackh[stackp] = (bH < height) ? height : bH;
                stackw[stackp] = mw;
                ix = ix + mw;
                stacko[stackp] = (unsigned int)offsets[i - 1] + ((height + bH - 1) / bH) * (pW / bW);
                stackp++;
                offsets[i] = offsets[i - 1] + (unsigned int)((width + bW - 1) / bW);
            } else if (stackp != 0) {
                // [C 0x1F6F64] into the stacked space
                t = stackp - 1;
                hw = width >> 1;
                a2 = (bW < hw) ? hw : bW;
                t5 = stacko[t];
                offsets[i] = stacko[t];
                if (a2 < stackw[t]) {
                    stacko[t] = t5 + (hw + bW - 1) / bW;
                    stackw[t] = stackw[t] - a2;
                } else {
                    // [C 0x1F6FE0]
                    hh = height >> 1;
                    a1 = (bH < hh) ? hh : bH;
                    if (a1 < stackh[t]) {
                        stacko[t] = ((hh + bH - 1) / bH) * (pW / bW) + t5;
                        stackh[t] = stackh[t] - a1;
                    } else {
                        stackp = t;
                    }
                }
                piviot = 1;
            } else {
                // [C 0x1F7054] down to the end of the level
                iy = iy + ((bH < height) ? height : bH);
                offsets[i] = offsets[i - 1] + (unsigned int)(((height + bH - 1) / bH) * (pW / bW));
            }
        } else {
            // [C 0x1F71BC] smaller than a page both ways
            mw = (bW < width) ? width : bW;
            bufstart[i] = bufstart[i - 1];
            bufwidth[i] = bufwidth[i - 1];
            bufheight[i] = bufheight[i - 1];
            if ((unsigned long long)(unsigned int)(ix + mw) < (bufwidth[i] << 6)) {
                // [C 0x1F71F8] push
                stackx[stackp] = ix;
                stackh[stackp] = (bH < height) ? height : bH;
                stackw[stackp] = mw;
                ix = ix + mw;
                stacko[stackp] = (unsigned int)offsets[i - 1] + ((height + bH - 1) / bH) * (pW / bW);
                stackp++;
                offsets[i] = offsets[i - 1] + (unsigned int)((width + bW - 1) / bW);
            } else if (iy + ((bH < height) ? height : bH) < bufheight[i] * pH && piviot == 0) {
                // [C 0x1F72CC] go down
                piviot = i;
                if (bH != 0) {
                    iy = iy + height;
                }
                offsets[i] = offsets[i - 1] + (unsigned int)(((height + bH - 1) / bH) * (pW / bW));
            } else {
                // [C 0x1F7328] into the stacked space (no stackp check in retail)
                t = stackp - 1;
                hw = width >> 1;
                a2 = (bW < hw) ? hw : bW;
                t5 = stacko[t];
                offsets[i] = stacko[t];
                if (a2 < stackw[t]) {
                    stacko[t] = t5 + (hw + bW - 1) / bW;
                    stackw[t] = stackw[t] - a2;
                } else {
                    // [C 0x1F7398]
                    hh = height >> 1;
                    a1 = (bH < hh) ? hh : bH;
                    if (a1 < stackh[t]) {
                        stacko[t] = ((hh + bH - 1) / bH) * (pW / bW) + t5;
                        stackh[t] = stackh[t] - a1;
                    } else {
                        stackp = t;
                    }
                }
            }
        }
        width >>= 1;
        height >>= 1;
    }

    // [C 0x1F7440] the palette's block: hidden in the last level's buffer when it fills it exactly, else after it
    m = maxlevel - 1;
    if ((bufwidth[m] << 6) == (unsigned long long)width && bufheight[m] * pH == height) {
        if ((unsigned int)(format - 0x13) < 2) {
            // [C 0x1F7498] start + (64 * bw / blockWidth) * height / blockHeight (64-bit)
            palette = bufstart[m] + (unsigned int)((((bufwidth[m] << 6) / bW) * (unsigned long long)height) / bH);
        } else {
            // [C 0x1F7608]
            palette = 0;
        }
    } else if (format == 0x13) {
        // [C 0x1F74F8] PSMT8: the bottom four blocks
        palette = bufstart[m] + ((unsigned int)((bufwidth[m] << 6) / pW) * bufheight[m] << 5)
                  - (unsigned int)((bufwidth[m] << 6) / pW) * (pW / bW) - 2;
    } else if (format == 0x14) {
        // [C 0x1F75A0] PSMT4: the bottom block
        palette = bufstart[m] + ((unsigned int)((bufwidth[m] << 6) / pW) * bufheight[m] << 5) - 1;
    } else {
        palette = 0;
    }

    // [C 0x1F760C] per level: the TRXPOS offset inside the buffer (or a new buffer start), the tiled fixup, the swizzle
    bs = bufstart[0];
    bw = (unsigned int)bufwidth[0];
    for (i = 0; i < maxlevel; i++) {
        if ((unsigned long long)bw == bufwidth[i]) {
            d = (offsets[i] - (unsigned long long)bs) * (unsigned long long)bW;
            bw64 = (unsigned int)(bw << 6);
            if ((d / bw64) * (unsigned long long)bH < 0x800) {
                // [C 0x1F7704]
                xyoff[i] = ((unsigned int)((d / bw64) * (unsigned long long)bH) << 16) | (unsigned int)(d % bw64);
                goto fixup;
            }
        }
        // [C 0x1F76D4]
        bw = (unsigned int)bufwidth[i];
        bs = bufstart[i];
        xyoff[i] = 0;
    fixup:
        // [C 0x1F7780]
        if ((bw << 6) / pW >= 2) {
            S1 = (bw << 11) / pW;
            S2 = pW / bW;
            S3 = (bw << 6) / bW;
            o = offsets[i];
            offsets[i] = (o & (unsigned long long)(unsigned int)~(S1 - S2))
                         | ((o & (unsigned long long)(unsigned int)(S3 - S2)) * (unsigned long long)(S1 / S3))
                         | ((o & (unsigned long long)(unsigned int)(S1 - S3)) / (unsigned long long)(S3 / S2));
        }
        // [C 0x1F7878] the swizzle (only for format < 0x3B), sign-extended from bit 31
        if (inTable) {
            offsets[i] = (unsigned long long)(long long)(int)func_001F6B08_inl4(format, (unsigned int)offsets[i]);
        } else {
            offsets[i] = (unsigned long long)(long long)(int)(unsigned int)offsets[i];
        }
    }

    // [C 0x1F7AF8] the palette with the last buffer width
    if ((bw << 6) / pW >= 2) {
        S3 = (bw << 6) / bW;
        S2 = pW / bW;
        S1 = (bw << 11) / pW;
        palette = (palette & ~(S1 - S2)) | ((palette & (S3 - S2)) * (S1 / S3)) | ((palette & (S1 - S3)) / (S3 / S2));
    }
    if (inTable) {
        palette = func_001F6B08_inl4(format, palette);
    }
    *paletteOffset = palette;
    // [C 0x1F7D4C] words: (start + (64 * bw / blockWidth) * (bufheight * pageHeight / blockHeight)) * 64
    *texMemSize = (unsigned int)(((unsigned long long)(long long)(int)bufstart[m]
                                  + ((bufwidth[m] << 6) / bW) * (unsigned long long)((bufheight[m] * pH) / bH)) << 6);
}

// [C] per level (0 .. maxMip) with 16-byte aligned pixels and rows of exactly width * depth / 8 bytes: the level's upload
// packet is built when there is none and the raster caches packets, and cleared when it does not; the pixel pointer
// advances by stride * height (halved per level), realigned to 16. Then the palette's packet (depth <= 8): built from
// palWidths / palHeights by depth with 32 bits for CPSM 0, 16 otherwise, kept only when cachePkts.
// 0x0020B648
extern "C" void skyPrepareUploadRaster(UnkStruct_RwRaster *raster)
{
    UnkStruct_skyRasterExt *ext = func_001F6588_inl2(raster);
    int depth = raster->field_14;
    unsigned int pixels;
    int contiguous;
    int i;
    int w;
    int h;
    int stride;
    unsigned int cpsm;
    int bits;
    unsigned int palW;
    unsigned int palH;

    contiguous = raster->field_18 == ((raster->field_C * depth) >> 3);
    pixels = (unsigned int)raster->field_4;
    i = 0;
    do {
        w = raster->field_C >> i;
        h = raster->field_10 >> i;
        stride = raster->field_18 >> i;
        if ((pixels & 0xF) == 0 && contiguous) {
            // [C 0x20B6F4]
            if (ext->field_3C[i] == 0) {
                if (ext->field_34 == 0) {
                    ext->field_3C[i] = 0;
                    goto next;
                }
                ext->field_3C[i] = _SkyBuildPktForUpLoadAlignedContiguousRectangle(w, h, (void *)pixels, depth,
                                                                                    ext->field_34);
            }
            if (ext->field_34 == 0) {
                ext->field_3C[i] = 0;
            }
        }
    next:
        pixels = pixels + (unsigned int)(stride * h);
        if ((pixels & 0xF) != 0) {
            pixels = (pixels + 0xF) & ~0xFu;
        }
        i++;
    } while (!((int)(ext->field_16 >> 2) < i));

    // [C 0x20B774]
    if (ext->field_2C == 0) {
        return;
    }
    if (!(raster->field_14 < 9)) {
        return;
    }
    palH = D_004B94F8[raster->field_14 - 1];
    palW = D_004B94D8[raster->field_14 - 1];
    cpsm = (ext->field_C >> 19) & 0xF;
    // [C 0x20B7C0] 2 -> 16, below 3 a movz on cpsm (0 -> 32, 1 -> 16), 3 and up -> 16
    bits = 0x10;
    if (cpsm == 0) {
        bits = 0x20;
    }
    if (ext->field_38 == 0) {
        if (ext->field_34 == 0) {
            ext->field_38 = 0;
            return;
        }
        ext->field_38 = _SkyBuildPktForUpLoadAlignedContiguousRectangle((int)palW, (int)palH, raster->field_8, bits,
                                                                        ext->field_34);
    }
    if (ext->field_34 != 0) {
        return;
    }
    ext->field_38 = 0;
}

// [C] the raster ends: the texture cache unlock (when locked) and release; rasters without their own memory (cFlags
// 0x80) stop there. Then the pending DMA references are waited out (two forced buffer swaps per pass), the original
// pixels freed (ext->field_0 cleared first), and the palette / mip upload packets freed and cleared. If ext->field_0 is
// nonzero after the free (0x1FA2FC; it was just cleared, so only when the free wrote it), the packet slots are only
// cleared. Returns 1. a0 and a2 are not read.
// 0x001FA168
extern "C" int SkyRasterDestroy(unsigned int pOut, UnkStruct_RwRaster *raster, int nIn)
{
    UnkStruct_skyRasterExt *ext = func_001F6588_inl2(raster);
    unsigned char *pixels;
    int k;

    if (ext->field_17 != 0) {
        RpSkyTexCacheRasterLock(raster, 0);
    }
    RpSkyTexCacheReleaseRaster(raster);
    if ((raster->field_21 & 0x80) != 0) {
        return 1;
    }
    ext = func_001F6588_inl2(raster);
    if (ext->field_0 > 0) {
        do {
            _rwDMAForceBufferSwap();
            _rwDMAForceBufferSwap();
        } while (ext->field_0 > 0);
    }
    pixels = raster->field_24;
    ext->field_0 = 0;
    if (pixels != 0) {
        ourGlobals.field_138(pixels);
    }
    if (ext->field_0 != 0) {
        // [C 0x1FA2FC]
        if (ext->field_38 != 0) {
            ext->field_38 = 0;
        }
        for (k = 0; k < 7; k++) {
            if (ext->field_3C[k] != 0) {
                ext->field_3C[k] = 0;
            }
        }
        return 1;
    }
    if (ext->field_38 != 0) {
        ourGlobals.field_138(ext->field_38);
        ext->field_38 = 0;
    }
    // [C 0x1FA228] the free function is re-read from ourGlobals for each call
    for (k = 0; k < 7; k++) {
        if (ext->field_3C[k] != 0) {
            ourGlobals.field_138(ext->field_3C[k]);
            ext->field_3C[k] = 0;
        }
    }
    return 1;
}

// [C] one packet qword (sq of a pcpyld pair)
static inline void func_001F7E08_inl1(UnkStruct_skyQword *p, unsigned long long lo, unsigned long long hi)
{
    p->field_0 = lo;
    p->field_8 = hi;
}

// [C] the raster's start: SkyGetRasterFormat, the extension cleared, the original size saved; a zero width or height
// gives a raster without memory (cFlags 0x80). By type (jtbl_004B8B50):
//   0 normal (0x1F96F8): stride, size and page-rounded cache size; the pixels malloc'd (unless cFlags 0x80).
//   1 Z buffer (0x1F97E4): no larger than the video mode in 64-pixel columns x rows (else 0x80000010); no memory.
//   2 camera (0x1F9850): within the video mode (or bz5120 set; else 0x80000010); no memory; stride from the mode.
//   3: 0. 4 texture (0x1F7F78), 5 camera texture (0x1F7EF0: no palette, no larger than the mode, then as 4):
//     TEX0 (field_8 / field_C) from the page size and log2 size, PSM / CPSM from the palette and pixel format; the
//     mipmap layout (calcOffsets) or one level; the palette's place; then either the new-style raster (one malloc of
//     the DMA upload chain + the pixels: a header {count, data pointer, count}, per level a GIF / TRXPOS / TRXREG /
//     TRXDIR / IMAGE block and a CNT + A+D + BITBLTBUF + REF tag group, merged when a level continues the previous
//     buffer; the palette the same; SyncDCache; cpPixels then points past the first block) or the old style (pixels +
//     palette malloc'd, skyPrepareUploadRaster).
// Errors: 0x8000000D (format), 0x80000010 (size), 0x80000013 (no memory: retail also passes the size); 0 then.
// 0x001F7E08
extern "C" int SkyRasterCreate(unsigned int pInOut, UnkStruct_RwRaster *raster, int flags)
{
    UnkStruct_skyRasterExt *ext = func_001F6588_inl2(raster);
    UnkStruct_skyRasterExt *ext2;
    UnkStruct_skyVideoMode *vm;
    unsigned int pageWidth;         // [C] fp
    unsigned int pageHeight;        // [C] t5
    unsigned int lsb;
    unsigned int msb;
    unsigned int x;
    int overW;
    int overH;
    int palHeight;                  // [C] sp+0x170
    int palWidth;                   // [C] sp+0x180
    int bpp;                        // [C] sp+0x160 bytes per palette pixel
    unsigned int palPageWidth;      // [C] t3
    unsigned int palPageHeight;     // [C] t1
    int stride;
    unsigned int psm;
    // [C] sp+0x20 offsetBy64[7], sp+0x60 widthBy64[7], sp+0xA0 xyoffset[7] ([G] 8 entries, zeroed here: retail
    // initializes only what it uses, and calcOffsets may leave xyoffset entries above its own level count unwritten)
    unsigned long long offsetBy64[8] = {0};
    unsigned long long widthBy64[8] = {0};
    unsigned int xyoffset[8] = {0};
    unsigned int texMemSize;        // [C] sp+0x150
    unsigned int paletteOffset;     // [C] sp+0x154
    unsigned int sysMemSize;        // [C] sp+0x190
    unsigned long long palPos;      // [C] sp+0x198: the palette's TRXPOS when it shares the last level's buffer
    int mintw;                      // [C] sp+0x158
    int minth;                      // [C] sp+0x15C
    int maxMip;
    int k;
    int w5;
    int h4;
    int w;
    int h;
    int n;
    int n5;
    int s0;
    unsigned int s5;
    unsigned int size;
    unsigned int base;
    unsigned int a;
    unsigned int *hdr;
    unsigned char f;
    unsigned long long v;
    unsigned long long t2;
    UnkStruct_skyQword *p0;         // [C] t0: the level blocks
    UnkStruct_skyQword *p3;         // [C] t3 / t7: the DMA tag list
    UnkStruct_skyQword *pal;        // [C] t1

    if (SkyGetRasterFormat(pInOut, raster, flags) == 0) {
        return 0;
    }
    if ((raster->field_23 & 0x60) == 0) {
        raster->field_21 = raster->field_21 & 0xBF;
    }
    raster->field_4 = 0;
    raster->field_8 = 0;
    ext->field_17 = 0;
    ext->field_34 = 0;
    ext->field_36 = 0;
    ext->field_38 = 0;
    ext->field_3C[0] = 0;
    raster->field_24 = raster->field_4;
    ext->field_3C[1] = 0;
    ext->field_3C[2] = 0;
    ext->field_3C[3] = 0;
    ext->field_3C[4] = 0;
    ext->field_3C[5] = 0;
    ext->field_3C[6] = 0;
    ext->field_58 = 0;
    raster->field_28 = raster->field_C;
    raster->field_2C = raster->field_10;
    if (raster->field_C == 0 || raster->field_10 == 0) {
        // [C 0x1F98F0]
        raster->field_18 = 0;
        raster->field_21 = 0x80;
        goto L_1F98FC;
    }

    switch (raster->field_20) {
    case 0:
        // [C 0x1F96F8] normal
        stride = (raster->field_14 * raster->field_C) >> 3;
        pageWidth = D_004B89B0[raster->field_14 - 1];
        pageHeight = D_004B8A30[raster->field_14 - 1];
        size = (unsigned int)(raster->field_10 * stride);
        raster->field_18 = stride;
        ext->field_28 = (int)size;
        ext->field_30 = (int)((((unsigned int)(raster->field_C - 1) + pageWidth) / pageWidth)
                              * (((unsigned int)(raster->field_10 - 1) + pageHeight) / pageHeight) << 11);
        if ((raster->field_21 & 0x80) != 0) {
            raster->field_4 = 0;
        } else {
            raster->field_4 = (unsigned char *)ourGlobals.field_134(size, 0x30411);
            if (raster->field_4 == 0) {
                func_001F6588_inl1(0x80000013);
                return 0;
            }
        }
        // [C 0x1F97D0]
        raster->field_24 = raster->field_4;
        raster->field_30 = raster->field_18;
        goto L_1F9900;
    case 1:
        // [C 0x1F97E4] Z buffer
        vm = skyVideoMode;
        if (((vm->field_0 + 0x3F) >> 6) * vm->field_4 < ((raster->field_C + 0x3F) >> 6) * raster->field_10) {
            func_001F6588_inl1(0x80000010);
            return 0;
        }
        raster->field_18 = 0;
        raster->field_21 = 0x80;
        raster->field_4 = 0;
        raster->field_24 = 0;
        goto L_1F98FC;
    case 2:
        // [C 0x1F9850] camera
        vm = skyVideoMode;
        if ((vm->field_0 < raster->field_C || vm->field_4 < raster->field_10) && bz5120 == 0) {
            func_001F6588_inl1(0x80000010);
            return 0;
        }
        // [C 0x1F98B8]
        raster->field_21 = 0x80;
        raster->field_4 = 0;
        raster->field_24 = 0;
        n = (((vm->field_8 + 7) >> 3) * ((vm->field_0 + 0x3F) >> 6)) << 6;
        raster->field_30 = n;
        raster->field_18 = n;
        goto L_1F9900;
    case 5:
        // [C 0x1F7EF0] camera texture: no palette, no larger than the video mode (64-pixel columns x rows)
        if ((raster->field_23 & 0x60) != 0) {
            func_001F6588_inl1(0x8000000D);
            return 0;
        }
        vm = skyVideoMode;
        if (((vm->field_0 + 0x3F) >> 6) * vm->field_4 < ((raster->field_C + 0x3F) >> 6) * raster->field_10) {
            func_001F6588_inl1(0x8000000D);
            return 0;
        }
        break;
    case 4:
        break;
    default:
        // [C 0x1F7E4C] type 3 and >= 6
        return 0;
    }

    // [C 0x1F7F78] texture
    pageHeight = D_004B8A30[raster->field_14 - 1];
    pageWidth = D_004B89B0[raster->field_14 - 1];
    // [C] overW / overH: log2 rounded up (the asm's shift loops; signed compare with the size)
    overW = -1;
    x = (unsigned int)raster->field_C;
    if (x != 0) {
        do {
            x >>= 1;
            overW++;
        } while (x != 0);
    }
    overW = overW + ((1 << overW) < raster->field_C);
    overH = -1;
    x = (unsigned int)raster->field_10;
    if (x != 0) {
        do {
            x >>= 1;
            overH++;
        } while (x != 0);
    }
    overH = overH + ((1 << overH) < raster->field_10);
    lsb = (((pageWidth < (unsigned int)raster->field_C) ? (unsigned int)raster->field_C >> 6 : pageWidth >> 6) << 14)
          | ((unsigned int)(overW & 0xF) << 26) | ((unsigned int)overH << 30);
    msb = (unsigned int)((overH & 0xF) >> 2);
    ext->field_14 = (unsigned short)skyDefaultMipmapKL;
    ext->field_34 = 1;
    ext->field_8 = lsb;
    ext->field_C = msb;

    switch (raster->field_23 & 0x60) {
    case 0:
        // [C 0x1F80A4] no palette: 1555 -> PSMCT16S + TCC, 888 -> PSMCT24, 8888 -> PSMCT32 + TCC
        palHeight = 0;
        palWidth = 0;
        palPageHeight = 0;
        bpp = 0;
        palPageWidth = 0;
        switch (raster->field_23 & 0xF) {
        case 1:
            ext->field_C = msb | 4;
            ext->field_8 = lsb | 0xA00000;
            break;
        case 5:
            ext->field_C = msb | 4;
            break;
        case 6:
            ext->field_8 = lsb | 0x100000;
            break;
        default:
            func_001F6588_inl1(0x8000000D);
            return 0;
        }
        break;
    case 0x40:
        // [C 0x1F8144] PAL4: PSMT4, TCC, CLD 1; 8 x 2 entries; CPSM 16S (2 bytes) or 32 (4 bytes)
        ext->field_8 = lsb | 0x1400000;
        msb = msb | 0x20000004;
        ext->field_C = msb;
        palHeight = 2;
        palWidth = 8;
        if ((raster->field_23 & 0xF) == 1) {
            bpp = 2;
            ext->field_C = msb | 0x500000;
            palPageWidth = 0x40;
            palPageHeight = 0x40;
        } else if ((raster->field_23 & 0xF) == 5) {
            palPageWidth = 0x40;
            bpp = 4;
            palPageHeight = 0x20;
        } else {
            func_001F6588_inl1(0x8000000D);
            return 0;
        }
        break;
    case 0x20:
        // [C 0x1F81D0] PAL8: PSMT8, TCC, CLD 1; 16 x 16 entries
        ext->field_8 = lsb | 0x1300000;
        msb = msb | 0x20000004;
        ext->field_C = msb;
        palWidth = 0x10;
        palHeight = 0x10;
        if ((raster->field_23 & 0xF) == 1) {
            bpp = 2;
            ext->field_C = msb | 0x500000;
            palPageWidth = 0x40;
            palPageHeight = 0x40;
        } else if ((raster->field_23 & 0xF) == 5) {
            palPageWidth = 0x40;
            bpp = 4;
            palPageHeight = 0x20;
        } else {
            func_001F6588_inl1(0x8000000D);
            return 0;
        }
        break;
    default:
        // [C 0x1F8268] both palette bits
        func_001F6588_inl1(0x8000000D);
        return 0;
    }

    // [C 0x1F8284]
    stride = (raster->field_14 * raster->field_C) >> 3;
    raster->field_18 = stride;
    if ((raster->field_23 & 0x80) != 0) {
        // [C 0x1F82A4] mipmapped: the 3.1 level loop (system memory, buffer widths, page-change alignment, block
        // offsets by PSM), then calcOffsets for the layout retail uses
        unsigned int lw = (unsigned int)raster->field_C;     // [C] s7
        unsigned int lh = (unsigned int)raster->field_10;    // [C] a3
        unsigned int levels = 0;                             // [C] t6
        unsigned int sizeBefore = 0;                         // [C] t1
        unsigned long long prevWidthBy64;                    // [C] t0
        unsigned int *table;
        unsigned long long word1;
        unsigned long long word2;
        int lstride;
        int mm;

        widthBy64[6] = 1;
        sysMemSize = 0;
        texMemSize = 0;
        offsetBy64[2] = 0;
        offsetBy64[1] = 0;
        offsetBy64[0] = 0;
        offsetBy64[5] = 0;
        offsetBy64[4] = 0;
        offsetBy64[3] = 0;
        offsetBy64[6] = 0;
        widthBy64[2] = 1;
        widthBy64[1] = 1;
        widthBy64[0] = 1;
        widthBy64[5] = 1;
        widthBy64[4] = 1;
        widthBy64[3] = 1;
        prevWidthBy64 = (unsigned int)((pageWidth < lw) ? lw >> 6 : pageWidth >> 6);
        if (lw != 0 && lh != 0 && skyMaxMipLevels != 0
            && (raster->field_C < 8 || raster->field_10 < 8 || (lw >= 8 && lh >= 8))) {
            do {
                // [C 0x1F8360]
                sysMemSize += ((((unsigned int)raster->field_14 * lw * lh) >> 3) + 0xF) & ~0xFu;
                widthBy64[levels] = (unsigned int)((pageWidth < lw) ? lw >> 6 : pageWidth >> 6);
                lstride = (int)(unsigned int)(((unsigned long long)(long long)raster->field_14
                                               * (widthBy64[levels] << 6)) >> 3);
                if (widthBy64[levels] != prevWidthBy64) {
                    // [C 0x1F8410] a new buffer width starts on a page
                    mm = (int)levels - 1;
                    texMemSize = (sizeBefore + ((((unsigned int)(raster->field_C >> mm) - 1 + pageWidth) / pageWidth)
                                                * (((unsigned int)(raster->field_10 >> mm) - 1 + pageHeight) / pageHeight)
                                                << 11)) & ~0x7FFu;
                }
                // [C 0x1F8460]
                {
                    unsigned int t = texMemSize + 0x3F;
                    unsigned int b4 = (t & 0x7C0) >> 8;

                    prevWidthBy64 = widthBy64[levels];
                    texMemSize = t & ~0x3Fu;
                    psm = (ext->field_8 >> 20) & 0x3F;
                    switch (psm) {   // [C] jtbl_004B8B70 (psm < 0x15)
                    case 0x00:
                    case 0x01:
                        table = D_004B8AB0;
                        break;
                    case 0x02:
                        table = D_004B8AD0;
                        break;
                    case 0x0A:
                        table = D_004B8AF0;
                        break;
                    case 0x13:
                        table = D_004B8B10;
                        break;
                    case 0x14:
                        table = D_004B8B30;
                        break;
                    default:
                        table = 0;
                        break;
                    }
                    if (table != 0) {
                        offsetBy64[levels] = (unsigned int)(((texMemSize >> 11) << 5) + table[b4]);
                    }
                }
                // [C 0x1F8528]
                sizeBefore = texMemSize;
                levels++;
                texMemSize = (sizeBefore + ((lh * (unsigned int)lstride) >> 2) + 0x3F) & ~0x3Fu;
                lw >>= 1;
                lh >>= 1;
            } while (lw != 0 && lh != 0 && levels < (unsigned int)skyMaxMipLevels
                     && (raster->field_C < 8 || raster->field_10 < 8 || (lw >= 8 && lh >= 8)));
        }
        // [C 0x1F85A4]
        calcOffsets((unsigned int)raster->field_C, (unsigned int)raster->field_10, (int)((ext->field_8 >> 20) & 0x3F),
                    offsetBy64, widthBy64, xyoffset, &texMemSize, &paletteOffset);
        word1 = (offsetBy64[1] & 0x3FFF) | (widthBy64[1] << 14) | ((offsetBy64[2] & 0x3FFF) << 20)
                | (widthBy64[2] << 34) | ((offsetBy64[3] & 0x3FFF) << 40) | (widthBy64[3] << 54);
        word2 = (offsetBy64[4] & 0x3FFF) | (widthBy64[4] << 14) | ((offsetBy64[5] & 0x3FFF) << 20)
                | (widthBy64[5] << 34) | ((offsetBy64[6] & 0x3FFF) << 40) | (widthBy64[6] << 54);
        n = palWidth * palHeight * bpp;
        ext->field_28 = (int)sysMemSize;
        ext->field_16 = (unsigned char)(((int)levels - 1) << 2);
        ext->field_18 = (unsigned int)word1;
        ext->field_1C = (unsigned int)(word1 >> 32);
        ext->field_20 = (unsigned int)word2;
        ext->field_24 = (unsigned int)(word2 >> 32);
        ext->field_2C = n;
        ext->field_30 = (int)texMemSize;
        if (n == 0) {
            ext->field_10 = 0;
        } else {
            // [C 0x1F86C8] the palette after the texture: a page more when it starts one
            ext->field_10 = paletteOffset;
            if ((paletteOffset << 6) == texMemSize) {
                ext->field_30 = (int)((paletteOffset << 6) + 0x800);
            }
        }
    } else {
        // [C 0x1F86E4] one level: MIPTBP words 0x4000 / 0x400004, page-rounded cache size, the palette at the end of
        // the last page when there is room (-4 blocks), else on its own page(s)
        unsigned int pagesW;
        unsigned int pagesH;
        unsigned int texSize;

        n = palWidth * palHeight * bpp;
        ext->field_24 = 0x400004;
        ext->field_1C = 0x400004;
        ext->field_20 = 0x4000;
        ext->field_18 = 0x4000;
        ext->field_28 = (int)(((unsigned int)(raster->field_10 * stride) + 0xF) & ~0xFu);
        ext->field_16 = 0;
        offsetBy64[0] = 0;
        xyoffset[0] = 0;
        pagesW = ((unsigned int)raster->field_C + pageWidth - 1) / pageWidth;
        widthBy64[0] = (unsigned int)(pagesW * (pageWidth >> 6));
        ext->field_2C = n;
        pagesH = ((unsigned int)raster->field_10 - 1 + pageHeight) / pageHeight;
        texSize = (pagesW * pagesH) << 11;
        ext->field_30 = (int)texSize;
        if (n == 0) {
            ext->field_10 = 0;
        } else if (raster->field_C < (int)(pagesW * pageWidth) || raster->field_10 < (int)(pagesH * pageHeight)) {
            // [C 0x1F87C4]
            ext->field_10 = (texSize >> 6) - 4;
        } else {
            // [C 0x1F87D0]
            ext->field_10 = texSize >> 6;
            ext->field_30 = (int)((texSize + ((((unsigned int)palWidth - 1 + palPageWidth) / palPageWidth)
                                              * (((unsigned int)palHeight - 1 + palPageHeight) / palPageHeight) << 11))
                                  & ~0x7FFu);
        }
    }

    // [C 0x1F8824]
    ext->field_28 = (ext->field_28 + 0xF) & ~0xF;
    if (skyNoNewStyleRasters != 0) {
        goto L_1F95FC;
    }
    if ((raster->field_21 & 0x80) != 0) {
        goto L_1F95FC;
    }
    if (!(((int)((unsigned int)raster->field_10 * (unsigned int)raster->field_C * (unsigned int)raster->field_14) >> 7)
          < 0x7FFF)) {
        goto L_1F95FC;
    }

    // [C 0x1F8878] new-style raster
    palPos = 0;
    ext->field_36 = ext->field_36 | 1;
    if (((ext->field_8 >> 20) & 0x3F) == 0x13) {
        // [C 0x1F8898] PSMT8 (twiddled), CPSM 32 with a 2-wide last level: the palette inside that level's buffer
        ext->field_36 = ext->field_36 | 2;
        if (((ext->field_C >> 19) & 0xF) == 0) {
            maxMip = ext->field_16 >> 2;
            if (widthBy64[maxMip] == 2) {
                unsigned int ob = (unsigned int)offsetBy64[maxMip];
                unsigned int pb = ext->field_10;
                unsigned int xy = xyoffset[maxMip];
                unsigned int tb;
                unsigned int dd;

                tb = (ob & ~0x1Eu) | ((ob & 2) << 2) | ((ob & 4) >> 1) | ((ob & 8) << 1) | ((ob & 0x10) >> 2);
                pb = (pb & ~0x1Eu) | ((pb & 2) << 2) | ((pb & 4) >> 1) | ((pb & 8) << 1) | ((pb & 0x10) >> 2);
                tb = tb - ((xy & 0x7FF) >> 4) - ((xy >> 17) & 0x3F8);
                dd = pb - tb;
                if (((dd >> 3) << 3) < 0x800) {
                    palPos = (unsigned long long)(((dd >> 3) << 19) | ((dd << 3) & 0x3F)) << 32;
                }
            }
        }
    }
    // [C 0x1F89A4]
    if (((ext->field_8 >> 20) & 0x3F) == 0x14) {
        // [C 0x1F89BC] PSMT4 (twiddled), CPSM 16
        ext->field_36 = ext->field_36 | 4;
        if (((ext->field_C >> 19) & 0xF) == 2) {
            maxMip = ext->field_16 >> 2;
            if (widthBy64[maxMip] != 2) {
                palPos = 0;
            } else {
                unsigned int ob = (unsigned int)offsetBy64[maxMip];
                unsigned int pb = ext->field_10;
                unsigned int xy = xyoffset[maxMip];
                unsigned int tb;
                unsigned int dd;

                tb = (ob & ~0x1Fu) | ((ob & 1) << 2) | ((ob & 2) >> 1) | ((ob & 4) << 1) | ((ob & 8) >> 2)
                     | (ob & 0x10);
                pb = (pb & ~0x1Fu) | ((pb & 1) << 2) | ((pb & 2) >> 1) | ((pb & 4) << 1) | ((pb & 8) >> 2)
                     | (pb & 0x10);
                tb = tb - ((xy & 0x7FF) >> 5) - ((xy >> 18) & 0x1FC);
                dd = pb - tb;
                if (((dd >> 2) << 3) < 0x800) {
                    palPos = (unsigned long long)(((dd >> 2) << 19) | ((dd << 4) & 0x3F)) << 32;
                } else {
                    palPos = 0;
                }
            }
        }
    }

    // [C 0x1F8AEC] the minimum transfer width / height by PSM (jtbl_004B8BD0), twiddled 16 x 4 / 32 x 4
    f = ext->field_36;
    psm = (ext->field_8 >> 20) & 0x3F;
    mintw = 0;
    minth = 1;
    ext->field_28 = (int)((((unsigned int)ext->field_16 >> 2) + 1) * 0x50);
    switch (psm) {
    case 0x00:
    case 0x30:
        mintw = 2;
        break;
    case 0x01:
    case 0x13:
    case 0x14:
    case 0x1B:
    case 0x24:
    case 0x2C:
    case 0x31:
        mintw = 8;
        break;
    case 0x02:
    case 0x0A:
    case 0x32:
    case 0x3A:
        mintw = 4;
        break;
    default:
        break;
    }
    if ((f & 2) != 0 && psm == 0x13) {
        mintw = 0x10;
        minth = 4;
    }
    if ((f & 4) != 0 && psm == 0x14) {
        mintw = 0x20;
        minth = 4;
    }
    // [C 0x1F8BA0] the levels' sizes at least the minimum transfer ([C] retail pads a short level's height to mintw,
    // not minth: `movz v1, t4, a1` with v1 = mintw)
    maxMip = ext->field_16 >> 2;
    w5 = raster->field_C;
    h4 = raster->field_10;
    for (k = 0; k <= maxMip; k++) {
        w = (w5 < mintw) ? mintw : w5;
        h = (h4 < minth) ? mintw : h4;
        n = (int)((unsigned int)w * (unsigned int)h * (unsigned int)raster->field_14) >> 3;
        ext->field_28 = ext->field_28 + ((n + 0xF) & ~0xF);
        w5 >>= 1;
        h4 >>= 1;
    }
    // [C 0x1F8DDC] the palette packet: a 2-row palette is sent as 3 rows
    if (ext->field_2C != 0) {
        if (palHeight == 2) {
            palHeight = 3;
        }
        ext->field_2C = palWidth * palHeight * bpp + 0x50;
    }
    // [C 0x1F8E10] the tag list: 0x10 + 0x40 per level that starts a buffer (xyoffset 0), + 0x40 for the palette
    s5 = 0x10;
    s0 = 0;
    maxMip = ext->field_16 >> 2;
    a = (unsigned int)ext->field_28;
    n = ext->field_2C;
    for (k = 0; k <= maxMip; k++) {
        if (xyoffset[k] == 0) {
            s5 += 0x40;
            s0++;
        }
    }
    if (n != 0) {
        s5 += 0x40;
        s0++;
        if (palPos == 0) {
            size = s5 + (((a + 0x7F) & ~0x7Fu) + 0x70) + (((unsigned int)n + 0x3F) & ~0x3Fu);
            goto L_1F8F70;
        }
        a = a + (unsigned int)n;
    }
    // [C 0x1F8F4C]
    size = s5 + (((a + 0x3F) & ~0x3Fu) + 0x70);
L_1F8F70:
    raster->field_4 = (unsigned char *)ourGlobals.field_134(size, 0x30411);
    if (raster->field_4 == 0) {
        func_001F6588_inl1(0x80000013);
        return 0;
    }
    // [C 0x1F8FE0] the header: {count, the 128-aligned data after the tag list, count}
    hdr = (unsigned int *)raster->field_4;
    hdr[0] = (unsigned int)s0;
    hdr[1] = ((unsigned int)hdr + ((unsigned int)s0 << 6) + 0x10 + 0x7F) & ~0x7Fu;
    hdr = (unsigned int *)raster->field_4;
    hdr[2] = (unsigned int)s0;
    if (ext->field_2C != 0 && palPos == 0) {
        raster->field_8 = (unsigned char *)((((unsigned int)hdr + s5 + 0x70) & ~0x7Fu)
                                            + (((unsigned int)ext->field_28 + 0x7F) & ~0x7Fu) + 0x50);
    }
    // [C 0x1F9044] per level
    base = (unsigned int)raster->field_4;
    p3 = (UnkStruct_skyQword *)(base + 0x10);
    hdr = (unsigned int *)base;
    a = hdr[1];
    p0 = (UnkStruct_skyQword *)a;
    w5 = raster->field_C;
    h4 = raster->field_10;
    k = 0;
    do {
        // [C 0x1F90A0] GIFtag NLOOP 3 EOP A+D; TRXPOS (twiddled: halved); TRXREG (twiddled: halved); TRXDIR 0
        func_001F7E08_inl1(p0, 0x1000000000000003ULL, 0xE);
        p0++;
        w = (w5 < mintw) ? mintw : w5;
        h = (h4 < minth) ? minth : h4;
        f = ext->field_36;
        psm = (ext->field_8 >> 20) & 0x3F;
        if (((f & 2) != 0 && psm == 0x13) || ((f & 4) != 0 && psm == 0x14)) {
            v = (unsigned long long)(xyoffset[k] & 0xFFFEFFFEu) << 31;
        } else {
            v = (unsigned long long)xyoffset[k] << 32;
        }
        func_001F7E08_inl1(p0, v, 0x51);
        p0++;
        f = ext->field_36;
        psm = (ext->field_8 >> 20) & 0x3F;
        if (((f & 2) != 0 && psm == 0x13) || ((f & 4) != 0 && psm == 0x14)) {
            v = (unsigned long long)(long long)(w >> 1) | ((unsigned long long)(unsigned int)(h >> 1) << 32);
        } else {
            v = (unsigned long long)(long long)w | ((unsigned long long)(unsigned int)h << 32);
        }
        func_001F7E08_inl1(p0, v, 0x52);
        p0++;
        func_001F7E08_inl1(p0, 0, 0x53);
        p0++;
        // [C 0x1F91D8] the IMAGE GIFtag (FLG 2) with the level's qword count
        n = (((int)((unsigned int)w * (unsigned int)h * (unsigned int)raster->field_14) >> 3) + 0xF) >> 4;
        v = (unsigned long long)(long long)n | 0x0800000000000000ULL;
        func_001F7E08_inl1(p0, v, 0);
        p0++;
        s0 = (int)(v & 0x7FFF);
        if (xyoffset[k] == 0) {
            // [C 0x1F9210] a new tag group: CNT 2 + DIRECT 2, GIFtag NLOOP 1 A+D, BITBLTBUF, REF to the level block
            func_001F7E08_inl1(p3, 0x10000002, 0x5000000200000000ULL);
            p3++;
            func_001F7E08_inl1(p3, 0x1000000000000001ULL, 0xE);
            p3++;
            f = ext->field_36;
            if ((f & 2) != 0 && ((ext->field_8 >> 20) & 0x3F) == 0x13) {
                // [C 0x1F9260] twiddled PSMT8 as PSMCT32, half width
                v = widthBy64[k] & 0x3E;
                t2 = (v << 15) | ((unsigned long long)(unsigned int)offsetBy64[k] << 32) | (v << 47);
            } else if ((f & 4) != 0 && ((ext->field_8 >> 20) & 0x3F) == 0x14) {
                // [C 0x1F92A8] twiddled PSMT4 as PSMCT16, half width
                v = widthBy64[k] & 0x3E;
                t2 = (v << 15) | (((unsigned long long)(unsigned int)offsetBy64[k] << 32) | 0x2000000) | (v << 47)
                     | 0x0200000000000000ULL;
            } else {
                // [C 0x1F92E0]
                psm = (ext->field_8 >> 20) & 0x3F;
                t2 = (widthBy64[k] << 16) | (psm << 24) | ((unsigned long long)(unsigned int)offsetBy64[k] << 32)
                     | (widthBy64[k] << 48) | ((unsigned long long)psm << 56);
            }
            // [C 0x1F9314]
            func_001F7E08_inl1(p3, t2, 0x50);
            p3++;
            func_001F7E08_inl1(p3, (unsigned long long)(unsigned int)((s0 + 5) | 0x30000000)
                                   | ((unsigned long long)((unsigned int)p0 - 0x50) << 32),
                               (unsigned long long)(unsigned int)((s0 + 5) | 0x50000000) << 32);
            p3++;
        } else {
            // [C 0x1F935C] the level continues the previous buffer: the previous REF tag's QWC and DIRECT count grow
            // (halfword read, word write: the high halves become 0x3000 / 0x5000)
            n5 = s0 + 5;
            *(unsigned int *)((char *)p3 - 0x10) = (unsigned int)(*(unsigned short *)((char *)p3 - 0x10) + n5)
                                                   | 0x30000000;
            *(unsigned int *)((char *)p3 - 0x4) = (unsigned int)(*(unsigned short *)((char *)p3 - 0x4) + n5)
                                                  | 0x50000000;
        }
        // [C 0x1F9398]
        p0 = p0 + s0;
        w5 >>= 1;
        h4 >>= 1;
        k++;
    } while (!((int)(ext->field_16 >> 2) < k));

    // [C 0x1F93C8] the palette's blocks before raster->field_8 (after the last level when it shares its buffer)
    if (ext->field_2C != 0) {
        if (palPos != 0) {
            raster->field_8 = (unsigned char *)((unsigned int)p0 + 0x50);
        }
        // [C 0x1F93E0]
        pal = (UnkStruct_skyQword *)raster->field_8;
        func_001F7E08_inl1(pal - 4, palPos, 0x51);
        func_001F7E08_inl1(pal - 5, 0x1000000000000003ULL, 0xE);
        func_001F7E08_inl1(pal - 3, (unsigned long long)(long long)palWidth
                                    | ((unsigned long long)(unsigned int)palHeight << 32), 0x52);
        func_001F7E08_inl1(pal - 2, 0, 0x53);
        n = (palWidth * palHeight * bpp) >> 4;
        v = (unsigned long long)(long long)n | 0x0800000000000000ULL;
        func_001F7E08_inl1(pal - 1, v, 0);
        s0 = (int)(v & 0x7FFF);
        func_001F7E08_inl1(p3, 0x10000002, 0x5000000200000000ULL);
        p3++;
        func_001F7E08_inl1(p3, 0x1000000000000001ULL, 0xE);
        p3++;
        if (palPos != 0) {
            if ((raster->field_21 & 0x40) != 0) {
                // [C 0x1F9494] the last level's BITBLTBUF again
                *p3 = p3[-4];
                n5 = s0 + 5;
                p3++;
            } else {
                // [C 0x1F949C] the last level's BITBLTBUF again; one tag less; the previous REF tag grows instead
                hdr = (unsigned int *)raster->field_4;
                n5 = s0 + 5;
                *p3 = p3[-4];
                p3++;
                hdr[0] = hdr[0] - 1;
                {
                    unsigned int q0 = *(unsigned short *)((char *)p3 - 0x40);
                    unsigned int q1 = *(unsigned short *)((char *)p3 - 0x34);

                    *(unsigned int *)((char *)p3 - 0x40) = (q0 + (unsigned int)n5) | 0x30000000;
                    *(unsigned int *)((char *)p3 - 0x34) = (q1 + (unsigned int)n5) | 0x50000000;
                }
            }
        } else {
            // [C 0x1F94FC] BITBLTBUF: the palette's block, width 1, CPSM both ways
            psm = (ext->field_C >> 19) & 0xF;
            t2 = ((unsigned long long)psm << 24) | (((unsigned long long)ext->field_10 << 32) | 0x10000)
                 | ((unsigned long long)psm << 56) | 0x0001000000000000ULL;
            func_001F7E08_inl1(p3, t2, 0x50);
            n5 = s0 + 5;
            p3++;
        }
        // [C 0x1F9548] the tag to the palette's blocks: CNT-coded (0x10000000) when cFlags 0x40, else REF
        if ((raster->field_21 & 0x40) != 0) {
            func_001F7E08_inl1(p3, (unsigned long long)(unsigned int)(n5 | 0x10000000)
                                   | ((unsigned long long)((unsigned int)pal - 0x50) << 32),
                               (unsigned long long)(unsigned int)(n5 | 0x50000000) << 32);
        } else {
            func_001F7E08_inl1(p3, (unsigned long long)(unsigned int)(n5 | 0x30000000)
                                   | ((unsigned long long)((unsigned int)pal - 0x50) << 32),
                               (unsigned long long)(unsigned int)(n5 | 0x50000000) << 32);
        }
    }
    // [C 0x1F95A4]
    base = (unsigned int)raster->field_4;
    if (raster->field_8 != 0) {
        a = (unsigned int)ext->field_2C + (unsigned int)raster->field_8 + 0x2F;
        SyncDCache((void *)base, (void *)a);
    } else {
        a = ((base + s5 + 0x70) & ~0x7Fu) + (((unsigned int)ext->field_28 + 0x7F) & ~0x7Fu);
        SyncDCache((void *)base, (void *)a);
    }
    goto L_1F96BC;

L_1F95FC:
    // [C 0x1F95FC] old style: cFlags 0x40 cleared; pixels + palette in one malloc (none with cFlags 0x80)
    raster->field_21 = raster->field_21 & 0xBF;
    if ((raster->field_21 & 0x80) != 0) {
        // [C 0x1F96AC]
        raster->field_4 = 0;
        raster->field_8 = 0;
        raster->field_18 = 0;
    } else {
        unsigned char *mem;

        mem = (unsigned char *)ourGlobals.field_134((unsigned int)(ext->field_28 + ext->field_2C), 0x30411);
        raster->field_4 = mem;
        if (mem == 0) {
            func_001F6588_inl1(0x80000013);
            return 0;
        }
        // [C 0x1F9668]
        if (ext->field_2C != 0) {
            raster->field_8 = mem + ext->field_28;
        }
        if (raster->field_14 == 8) {
            ext->field_36 = ext->field_36 | 2;
        }
        skyPrepareUploadRaster(raster);
    }

L_1F96BC:
    // [C 0x1F96BC] the originals; a new-style raster's pixels then start past the first level block (data + 0x50)
    base = (unsigned int)raster->field_4;
    raster->field_24 = raster->field_4;
    raster->field_30 = raster->field_18;
    if ((ext->field_36 & 1) == 0) {
        goto L_1F9900;
    }
    if ((raster->field_21 & 0x80) != 0) {
        goto L_1F9900;
    }
    hdr = (unsigned int *)base;
    a = hdr[1] + 0x50;
    raster->field_4 = (unsigned char *)a;
    goto L_1F9900;

L_1F98FC:
    raster->field_30 = 0;
L_1F9900:
    // [C 0x1F9900] skyRasterExt re-read
    ext2 = func_001F6588_inl2(raster);
    ext2->field_4 = 0;
    ext2->field_0 = 0;
    return 1;
}

// ---------------------------------------------------------------------------------------------------------------
// boot2 additions (track rw): the sub-raster standard function.

extern unsigned long long skyFrame_1;   // [C] gp 0x004E1838: FRAME_1 shadow (8 bytes)

// [C] the asm's shift loop: -1 for 0, else the index of the top set bit (the loop counts the shifts, starting at -1)
static inline int func_001F9DC0_inl1(unsigned int v)
{
    int n = -1;

    if (v != 0) {
        do {
            v >>= 1;
            n++;
        } while (v != 0);
    }
    return n;
}

// [C] (raster, parent, flags) -> 1, or 0 (no error set) when the parent is a twiddled PSMT8 ext (+0x36 bit 1) and the
// sub raster's offset word (+0x1C, x low half / y high half) has any of the bits 0x70015. Steps: for a type 4 raster
// ext +0x36 bit 0 is cleared and bit 1 copied from the parent's; the format byte +0x23, stride +0x18 and depth +0x14
// are copied from the parent. A camera-texture (type 5) sub raster of a camera (type 2) parent: flags = parent flags
// | 0x80; the x offset is trimmed to a multiple of 64 (the removed part added to the width), the width rounded up to
// a power of two and clipped to the parent; the y offset is trimmed to a multiple of 0x140 / (stride >> 8), likewise
// the height; then TEX0 = (x offset >> 1 + y offset * (stride >> 8)) & 0x1FF as TBP, FRAME_1 bits 16..21 as TBW
// << 14 and bits 24..29 as PSM << 20, TW = log2 width << 26, TH = log2 height << 30 (high word bits from
// log2 height >> 2), TEX1 byte +0x16 cleared. Any other pair: the raster's flags byte is the parent's. Finally the
// pixels pointer is the top raster's (parent +0) pixels + stride * y offset + (x offset * ((depth + 3) >> 2)) >> 1, the
// originals (+0x24/+0x28/+0x2C/+0x30) are set from the new geometry and the palette is the top raster's.
// [L] 0x140 / (stride >> 8): the asm divides by zero (break 7) when the parent's stride < 0x100; not expressed.
// 0x001F9DC0
extern "C" int func_001F9DC0(UnkStruct_RwRaster *raster, UnkStruct_RwRaster *parent, int flags)
{
    UnkStruct_skyRasterExt *ext = func_001F6588_inl2(raster);
    UnkStruct_skyRasterExt *pext = func_001F6588_inl2(parent);
    UnkStruct_RwRaster *top = (UnkStruct_RwRaster *)parent->field_0;
    unsigned int v;
    int w;
    int a2;
    unsigned int pflags;
    unsigned int x;
    unsigned int y;
    unsigned long long fr;
    unsigned int t0;
    int stride8;

    if ((raster->field_20 & 7) == 4) {
        v = ext->field_36 & 0xFE;
        if ((pext->field_36 & 2) != 0) {
            v = v | 2;
        } else {
            v = v & 0xFD;
        }
        ext->field_36 = (unsigned char)v;
    }
    if ((pext->field_36 & 2) != 0) {
        if ((*(unsigned int *)&raster->field_1C & 0x70015) != 0) {
            return 0;
        }
        ext->field_36 = ext->field_36 | 2;
    }
    raster->field_23 = parent->field_23;
    raster->field_18 = parent->field_18;
    raster->field_14 = parent->field_14;
    pflags = parent->field_20;
    if ((pflags & 7) == 2 && (raster->field_20 & 7) == 5) {
        // [C 0x1F9E7C] camera texture inside a camera
        raster->field_20 = (unsigned char)(pflags | 0x80);
        x = (unsigned short)raster->field_1C;
        a2 = (int)(x & 0x3F);
        raster->field_1C = (short)(x - a2);
        w = raster->field_C + a2;
        raster->field_C = w;
        a2 = func_001F9DC0_inl1((unsigned int)w);
        w = raster->field_C;
        a2 = w & ((1 << a2) - 1);
        if (a2 != 0) {
            raster->field_C = (w - a2) << 1;
        }
        a2 = (int)(raster->field_C + raster->field_1C) - parent->field_C;
        if (a2 > 0 && a2 < raster->field_1C) {
            raster->field_1C = (short)((unsigned short)raster->field_1C - a2);
        }
        stride8 = parent->field_18 >> 8;
        y = (unsigned short)raster->field_1E;
        // [C] break 7 when stride8 == 0 (not expressed)
        a2 = (int)(0x140 / stride8) - 1;
        a2 = (int)(raster->field_1E & a2);
        w = raster->field_10 + a2;
        raster->field_1E = (short)(y - a2);
        raster->field_10 = w;
        a2 = func_001F9DC0_inl1((unsigned int)w);
        w = raster->field_10;
        a2 = w & ((1 << a2) - 1);
        if (a2 != 0) {
            raster->field_10 = (w - a2) << 1;
        }
        a2 = (int)(raster->field_1E + raster->field_10) - parent->field_10;
        if (a2 > 0 && a2 < raster->field_1E) {
            raster->field_1E = (short)((unsigned short)raster->field_1E - a2);
        }
        stride8 = parent->field_18 >> 8;
        fr = skyFrame_1;
        v = (unsigned int)(raster->field_1E * stride8) + (((unsigned short)raster->field_1C & 0xFFF) >> 1);
        v = (v & 0x1FF) | (((unsigned int)(fr >> 16) & 0x3F) << 14) | (((unsigned int)(fr >> 24) & 0x3F) << 20);
        ext = func_001F6588_inl2(raster);
        ext->field_8 = v;
        t0 = (unsigned int)func_001F9DC0_inl1((unsigned int)raster->field_C);
        ext->field_C = 0;
        ext->field_8 = ext->field_8 | ((t0 & 0xF) << 26);
        t0 = (unsigned int)func_001F9DC0_inl1((unsigned int)raster->field_10);
        ext->field_8 = ext->field_8 | (t0 << 30);
        t0 = (unsigned int)func_001F9DC0_inl1((unsigned int)raster->field_10);
        ext->field_16 = 0;
        ext->field_C = ext->field_C | ((t0 & 0xF) >> 2);
    } else {
        raster->field_20 = (unsigned char)pflags;
    }
    // [C 0x1F9EF0 / 0x1FA104]
    raster->field_30 = raster->field_18;
    raster->field_28 = raster->field_C;
    raster->field_2C = raster->field_10;
    raster->field_4 = top->field_4 + top->field_18 * raster->field_1E +
                      ((raster->field_1C * ((top->field_14 + 3) >> 2)) >> 1);
    raster->field_24 = raster->field_4;
    raster->field_8 = top->field_8;
    return 1;
}
