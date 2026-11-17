// RenderWare 3.6 sky2 driver: the frame buffer rasters and the small RpSky* API next to them (RW 3.1 file name:
// driver/sky2/basky.c; reading aid only). Written from the retail asm (decomp assembly/asm/rw/renderware.s).
// Tags: [C] confirmed from the asm, [L], [G]. boot2 (track rw); spec: docs/results/boot1-2026-11-05/rw/sky2_handlers.md
// section 8c (the asm wins).

#include "rw/skyglobals.h"

// [C] one 0x18-byte RwVideoMode view (customMode's two halves) ([L] RwVideoMode)
struct UnkStruct_videoModes {
    int field_0;            // [L] width
    int field_4;            // [L] height
    int field_8;            // [L] depth
    unsigned int field_C;   // [L] flags
    int field_10;           // [L] refRate
    int field_14;           // [L] format
};
// [C] customMode: 0x34 bytes (symbol table) ([L] RwSkyVideoMode)
struct UnkStruct_customMode {
    UnkStruct_videoModes field_0;   // [C] the draw buffer's mode
    UnkStruct_videoModes field_18;  // [C] the display buffer's mode
    unsigned char field_30;
    unsigned char field_31;
    unsigned char field_32;
    unsigned char field_33;
};
// [C] _rwDMAFlipData: 0x3D0 bytes, read here as 64-bit words q[offset / 8] (an sceGsDBuffDc first: +0x10 / +0x38
// the two DISPFB values, +0x60 / +0x1D0 the two FRAME values [C SkyStart])
struct UnkStruct_rwDMAFlipData {
    unsigned long long q[0x7A];
};

// [C] the raster fields used here ([L] RwRaster: +0xC width, +0x10 height, +0x20 cType, +0x23 cFormat)
struct UnkStruct_skyRaster {
    unsigned char pad_0[0xC];
    unsigned int field_C;           // [C] width (rounded up to a power of two here)
    unsigned int field_10;          // [C] height (likewise)
    unsigned char pad_14[0xC];
    unsigned char field_20;         // [C] 0x85 for the frame buffer rasters ([L] cType)
    unsigned char pad_21[0x2];
    unsigned char field_23;         // [C] tested << 8 for 0x2000 / 0x4000 ([L] cFormat: rwRASTERFORMATPAL8 / PAL4)
};
// [C] one 0xC-byte cache slot (skyFakeCache*: symbol sizes; texcache entries have the same shape)
struct UnkStruct_skyFakeCache {
    UnkStruct_skyRaster *field_0;   // [C] the raster that holds the slot
    unsigned int field_4;           // [C] the GS word address
    unsigned int field_8;
};
// [C] the driver's raster extension at raster + skyRasterExt (offsets from the asm)
struct UnkStruct_skyRasterExt {
    unsigned char pad_0[0x8];
    unsigned int field_8;           // [C] TBP0 (bits 0-13), PSM << 20, TBW << 14, log2 w << 26, log2 h << 30 ([L] TEX0 low)
    unsigned int field_C;           // [C] CBP in bits 5-18 ([L] TEX0 high)
    unsigned int field_10;          // [C] added to the slot address >> 6 for the CBP
    unsigned short field_14;        // [C] K in bits 0-11, L in bits 12-13 ([L] TEX1)
    unsigned char field_16;
    unsigned char field_17;
    unsigned char pad_18[0x1E];
    unsigned char field_36;         // [C] bits 1 / 2: twiddled ([L])
    unsigned char pad_37[0x21];
    UnkStruct_skyFakeCache *field_58;   // [C] the cache slot holding the raster, or 0
};
// [C] the texture fields used by the mipmap K / L functions ([L] RwTexture: +0 raster)
struct UnkStruct_skyTexture {
    UnkStruct_skyRaster *field_0;
};
// [C] ourGlobals + 0x18 ([L] RwGlobals.dOpenDevice.zBufferFar)
struct UnkStruct_ourGlobals {
    unsigned char pad_0[0x18];
    float field_18;
};

extern UnkStruct_customMode customMode;              // [C] 0x00675F10
extern UnkStruct_rwDMAFlipData _rwDMAFlipData;       // [C] 0x006754C0
extern UnkStruct_ourGlobals ourGlobals;              // [C] 0x00670BD0
extern UnkStruct_skyFakeCache skyFakeCacheFrameBuffer0;     // [C] 0x00675F48
extern UnkStruct_skyFakeCache skyFakeCacheFrameBuffer1;     // [C] 0x00675F58
extern UnkStruct_skyFakeCache skyFakeCacheDisplayBuffer0;   // [C] 0x00675F68
extern UnkStruct_skyFakeCache skyFakeCacheDisplayBuffer1;   // [C] 0x00675F78
extern int engineStarted;                            // [C] gp 0x004E2880
extern int skyRasterExt;                             // [C] gp 0x004E2858
extern int skyFrameBit;                              // [C] gp 0x004E188C
extern unsigned long long skyFrame_1;                // [C] gp 0x004E1838 (FRAME_1 shadow, 8 bytes)
extern unsigned char bz5120;                         // [C] gp 0x004E1814 (read by SkyRasterCreate)
extern int skySuspended;                             // [C] gp 0x004E182C
extern int skyFSAA0VisibleWidth;                     // [C] gp 0x004E18A8
extern int zBufferDepth;                             // [C] gp 0x004E18A4
extern unsigned int skyDefaultMipmapKL;              // [C] gp 0x004E18A0
extern int skyTLClipperMode;                         // [C] gp 0x004E18E0
extern int D_004E18E4;                               // [C] gp 0x004E18E4 (.sdata)
extern UnkStruct_customMode *skyVideoMode;           // [C] gp 0x004E1808
extern int skyDitherState;                           // [C] gp 0x004E2884
extern unsigned long long skyAlpha_1;                // [C] gp 0x004E1868 (ALPHA_1 shadow, 8 bytes)
extern unsigned long long skyTest_1;                 // [C] gp 0x004E1840 (TEST_1 shadow, 8 bytes)
extern int D_004E28B0;                               // [C] gp 0x004E28B0 (.sbss; 1 after SkyStart)
extern float D_004E28B4;                             // [C] gp 0x004E28B4 (.sbss)
extern int skyMaxMipLevels;                          // [C] gp 0x004E1888
extern float D_004E0734;                             // [C] 16777215.0 (24-bit Z far)
extern float D_004E0738;                             // [C] 65535.0 (16-bit Z far)

// [C] RW core (baraster.cpp), reaching the driver's SkyRasterCreate
extern "C" UnkStruct_skyRaster *RwRasterCreate(int width, int height, int depth, int flags);
extern "C" void _rwDMAUnhook(void);
extern "C" void _rpSkyFrameBufferRastersUpdate(void);

// [C] the index of the highest set bit (-1 for 0), the loop the asm inlines (srl until zero)
static int skyFbMSB(unsigned int v)
{
    int n = -1;

    while (v != 0) {
        v = v >> 1;
        n++;
    }
    return n;
}

// [C] the raster's width and height rounded up to a power of two (each written only when it was not one; the mask
// uses the shift amount's low 5 bits as sllv does), then the extension's TEX0 words from psm / fbw, word +0xC =
// (log2 h & 0xF) >> 2, byte +0x17 = 1, byte +0x16 = 0. Shared by SkyCreateFBRaster / SkyCreateDBRaster.
static void skyFbRasterSetup(UnkStruct_skyRaster *r, unsigned int psm, unsigned int fbw)
{
    UnkStruct_skyRasterExt *x;
    unsigned int v;
    unsigned int low;
    int n;

    n = skyFbMSB(r->field_C);
    v = r->field_C;
    low = v & ((1u << (n & 0x1F)) - 1);
    if (low != 0) {
        r->field_C = (v - low) << 1;
    }
    v = r->field_10;
    n = skyFbMSB(v);
    low = v & ((1u << (n & 0x1F)) - 1);
    if (low != 0) {
        r->field_10 = (v - low) << 1;
    }
    x = (UnkStruct_skyRasterExt *)((char *)r + skyRasterExt);
    x->field_8 = (fbw << 14) | (psm << 20);
    n = skyFbMSB(r->field_C);
    v = x->field_8;
    x->field_C = 0;
    x->field_8 = v | ((unsigned int)(n & 0xF) << 26);
    n = skyFbMSB(r->field_10);
    x->field_8 = x->field_8 | ((unsigned int)n << 30);
    n = skyFbMSB(r->field_10);
    v = x->field_C | (unsigned int)((n & 0xF) >> 2);
    x->field_17 = 1;
    x->field_C = v;
    x->field_16 = 0;
}

// [C] for the display raster (with the display slot), then the draw raster (with the frame slot), each when set: the
// slot it held is released (its +0 and the extension's +0x58 cleared), then TBP0 = slot address >> 6 and CBP =
// (that + ext +0x10) & 0x3FFF, the slot takes the raster. The *0 slots when skyFrameBit & 1, else the *1 slots.
// skyRasterExt is read once per raster. No defined result.
// 0x001FE678
extern "C" void _rpSkyFrameBufferRastersUpdate(void)
{
    UnkStruct_skyFakeCache *disp;
    UnkStruct_skyFakeCache *frame;
    UnkStruct_skyRasterExt *x;
    unsigned int v;

    if ((skyFrameBit & 1) != 0) {
        disp = &skyFakeCacheDisplayBuffer0;
        frame = &skyFakeCacheFrameBuffer0;
    } else {
        disp = &skyFakeCacheDisplayBuffer1;
        frame = &skyFakeCacheFrameBuffer1;
    }
    if (skyDisplayRaster != 0) {
        x = (UnkStruct_skyRasterExt *)((char *)skyDisplayRaster + skyRasterExt);
        if (x->field_58 != 0) {
            x->field_58->field_0 = 0;
            x->field_58 = 0;
        }
        v = disp->field_4 >> 6;
        x->field_C = (x->field_C & 0xFFF8001F) | (((v + x->field_10) & 0x3FFF) << 5);
        disp->field_0 = (UnkStruct_skyRaster *)skyDisplayRaster;
        x->field_8 = (x->field_8 & 0xFFFFC000) | (v & 0x3FFF);
        x->field_58 = disp;
    }
    if (skyDrawRaster != 0) {
        x = (UnkStruct_skyRasterExt *)((char *)skyDrawRaster + skyRasterExt);
        if (x->field_58 != 0) {
            x->field_58->field_0 = 0;
            x->field_58 = 0;
        }
        v = frame->field_4 >> 6;
        x->field_C = (x->field_C & 0xFFF8001F) | (((v + x->field_10) & 0x3FFF) << 5);
        frame->field_0 = (UnkStruct_skyRaster *)skyDrawRaster;
        x->field_8 = (x->field_8 & 0xFFFFC000) | (v & 0x3FFF);
        x->field_58 = frame;
    }
}

// [C] the draw buffer's raster: RwRasterCreate(draw width, height, depth, format | 2) (not NULL-checked), cType 0x85,
// the power-of-two rounding and TEX0 from FRAME_1's shadow (PSM bits 24-29, FBW bits 16-21). Returns the raster.
// 0x001FE7A8
extern "C" UnkStruct_skyRaster *SkyCreateFBRaster(void)
{
    UnkStruct_skyRaster *r;
    unsigned long long f;

    r = RwRasterCreate(customMode.field_0.field_0, customMode.field_0.field_4, customMode.field_0.field_8,
                       customMode.field_0.field_14 | 2);
    r->field_20 = 0x85;
    f = skyFrame_1;
    // [C 0x1FE874] skyFrame_1 is read after the rounding; nothing in between writes it
    skyFbRasterSetup(r, (unsigned int)((f >> 24) & 0x3F), (unsigned int)((f >> 16) & 0x3F));
    return r;
}

// [C] the display buffer's raster: bz5120 = 1 around RwRasterCreate(display width, height, depth, format | 2) (not
// NULL-checked; SkyRasterCreate reads it), cType 0x85, the rounding and TEX0 from the first DISPFB (_rwDMAFlipData
// +0x10: PSM bits 15-19, FBW bits 9-14). Returns the raster.
// 0x001FE988
extern "C" UnkStruct_skyRaster *SkyCreateDBRaster(void)
{
    UnkStruct_skyRaster *r;
    unsigned long long d;

    bz5120 = 1;
    r = RwRasterCreate(customMode.field_18.field_0, customMode.field_18.field_4, customMode.field_18.field_8,
                       customMode.field_18.field_14 | 2);
    bz5120 = 0;
    r->field_20 = 0x85;
    d = _rwDMAFlipData.q[0x10 / 8];
    // [C 0x1FEA64..0x1FEAA0] the DISPFB is read after the rounding; it is not written in between
    skyFbRasterSetup(r, (unsigned int)((d >> 15) & 0x1F), (unsigned int)((d >> 9) & 0x3F));
    return r;
}

// [C] only while the engine is started (else returns 0): the four fake cache slots get the GS word addresses of the
// two FRAME buffers (_rwDMAFlipData +0x60 / +0x1D0) and the two DISPFB buffers (+0x10 / +0x38): (base & 0x1FF) << 11,
// their raster word cleared; then skyDrawRaster = SkyCreateFBRaster(), skyDisplayRaster = SkyCreateDBRaster() (not
// NULL-checked) and _rpSkyFrameBufferRastersUpdate. Returns 1.
// 0x001FEB78
extern "C" int _rpSkyCreateFrameBufferRasters(void)
{
    unsigned long long *q;

    if (engineStarted == 0) {
        return 0;
    }
    q = _rwDMAFlipData.q;
    skyFakeCacheFrameBuffer0.field_4 = (unsigned int)(q[0x60 / 8] & 0x1FF) << 11;
    skyFakeCacheFrameBuffer1.field_4 = (unsigned int)(q[0x1D0 / 8] & 0x1FF) << 11;
    skyFakeCacheDisplayBuffer0.field_4 = (unsigned int)(q[0x10 / 8] & 0x1FF) << 11;
    skyFakeCacheDisplayBuffer1.field_4 = (unsigned int)(q[0x38 / 8] & 0x1FF) << 11;
    skyFakeCacheFrameBuffer0.field_0 = 0;
    skyFakeCacheFrameBuffer1.field_0 = 0;
    skyFakeCacheDisplayBuffer0.field_0 = 0;
    skyFakeCacheDisplayBuffer1.field_0 = 0;
    skyDrawRaster = SkyCreateFBRaster();
    skyDisplayRaster = SkyCreateDBRaster();
    _rpSkyFrameBufferRastersUpdate();
    return 1;
}

// 0x001FF3C8
extern "C" UnkStruct_customMode *RpSkyGetModeStructurePtr(void)
{
    return &customMode;
}

// [C] the index of the highest set bit of a0, -1 for 0.
// 0x001FF3D8
extern "C" int _rwSkyFindMSB(unsigned int v)
{
    return skyFbMSB(v);
}

// [C] the FSAA visible width: accepted (stored, returns 1) only while the engine is stopped and for one of the widths
// 0x100, 0x140, 0x180, 0x200, 0x280; else returns 0.
// 0x001FF408
extern "C" int func_001FF408(int width)
{
    int ok = 0;

    if (engineStarted == 0) {
        if (width == 0x100 || width == 0x140 || width == 0x180 || width == 0x200 || width == 0x280) {
            ok = 1;
        }
    }
    if (ok != 0) {
        skyFSAA0VisibleWidth = width;
    }
    return ok;
}

// [C] when not suspended: _rwDMAUnhook, skySuspended = 1, returns 1; else returns 0.
// 0x001FF458
extern "C" int RpSkySuspend(void)
{
    if (skySuspended != 0) {
        return 0;
    }
    _rwDMAUnhook();
    skySuspended = 1;
    return 1;
}

// [C] (ext byte +0x36 & 6) != 0
// 0x001FF490
extern "C" int func_001FF490(UnkStruct_skyRaster *r)
{
    UnkStruct_skyRasterExt *x = (UnkStruct_skyRasterExt *)((char *)r + skyRasterExt);

    return (x->field_36 & 6) != 0;
}

// [C] for a raster whose cFormat << 8 has 0x2000 (else 0x4000): ext byte +0x36 bit 1 (else bit 2) set, then cleared
// again (a second store) when on == 0; returns the raster. Neither format bit: returns 0, nothing written.
// 0x001FF4A8
extern "C" UnkStruct_skyRaster *_skyRasterSetTwiddling(UnkStruct_skyRaster *r, int on)
{
    UnkStruct_skyRasterExt *x = (UnkStruct_skyRasterExt *)((char *)r + skyRasterExt);
    unsigned int fmt = (unsigned int)r->field_23 << 8;
    unsigned char b;

    if ((fmt & 0x2000) != 0) {
        b = (unsigned char)(x->field_36 | 2);
        x->field_36 = b;
        if (on == 0) {
            x->field_36 = (unsigned char)(b & 0xFD);
        }
        return r;
    }
    if ((fmt & 0x4000) != 0) {
        b = (unsigned char)(x->field_36 | 4);
        x->field_36 = b;
        if (on == 0) {
            x->field_36 = (unsigned char)(b & 0xFB);
        }
        return r;
    }
    return 0;
}

// [C] refused (returns 0) while the engine is started; else zBufferDepth = 32 with Z far D_004E0734 (deep != 0) or
// 16 with D_004E0738, the far value into ourGlobals + 0x18; returns 1.
// 0x001FF510
extern "C" int RpSkySelectDeepZBuffer(int deep)
{
    int depth;
    float zfar;

    if (engineStarted != 0) {
        return 0;
    }
    if (deep != 0) {
        zfar = D_004E0734;
        depth = 0x20;
    } else {
        zfar = D_004E0738;
        depth = 0x10;
    }
    zBufferDepth = depth;
    ourGlobals.field_18 = zfar;
    return 1;
}

// [C] k * 16 truncated (cvt.w.s), clamped to -0x800..0x7FF, into skyDefaultMipmapKL bits 0-11; returns the clamped
// value * 0.0625.
// 0x001FF550
extern "C" float func_001FF550(float k)
{
    int v = (int)(k * 16.0f);
    float f;

    if (v < -0x800) {
        v = -0x800;
    } else if (v >= 0x800) {
        v = 0x7FF;
    }
    f = (float)v;
    skyDefaultMipmapKL = (skyDefaultMipmapKL & 0xFFFFF000) | ((unsigned int)v & 0xFFF);
    return f * 0.0625f;
}

// [C] l (3 when >= 4, unsigned) into skyDefaultMipmapKL bits 12-13; returns that l.
// 0x001FF5B8
extern "C" unsigned int func_001FF5B8(unsigned int l)
{
    unsigned int v = (l < 4) ? l : 3;

    skyDefaultMipmapKL = (skyDefaultMipmapKL & 0xFFFFCFFF) | (v << 12);
    return v;
}

// [C] skyDefaultMipmapKL bits 0-11 sign-extended, * 0.0625
// 0x001FF5E8
extern "C" float func_001FF5E8(void)
{
    unsigned int kl = skyDefaultMipmapKL;
    int v = (int)(kl & 0xFFF);

    if ((kl & 0x800) != 0) {
        v = v | -0x1000;
    }
    return (float)v * 0.0625f;
}

// [C] skyDefaultMipmapKL bits 12-13
// 0x001FF618
extern "C" unsigned int func_001FF618(void)
{
    return (skyDefaultMipmapKL & 0x3000) >> 12;
}

// [C] a texture's K: when the texture and its raster exist, k * 16 truncated and clamped as func_001FF550 into ext
// halfword +0x14 bits 0-11; returns the texture, else 0.
// 0x001FF628
extern "C" UnkStruct_skyTexture *func_001FF628(UnkStruct_skyTexture *tex, float k)
{
    UnkStruct_skyRasterExt *x;
    int v;

    if (tex == 0 || tex->field_0 == 0) {
        return 0;
    }
    v = (int)(k * 16.0f);
    if (v < -0x800) {
        v = -0x800;
    } else if (v >= 0x800) {
        v = 0x7FF;
    }
    x = (UnkStruct_skyRasterExt *)((char *)tex->field_0 + skyRasterExt);
    x->field_14 = (unsigned short)((x->field_14 & 0xF000) | ((unsigned int)v & 0xFFF));
    return tex;
}

// [C] a texture's L (3 when >= 4, unsigned) into ext halfword +0x14 bits 12-13; returns the texture, or 0 when it or
// its raster is NULL.
// 0x001FF698
extern "C" UnkStruct_skyTexture *func_001FF698(UnkStruct_skyTexture *tex, unsigned int l)
{
    UnkStruct_skyRasterExt *x;
    unsigned int v;

    if (tex == 0 || tex->field_0 == 0) {
        return 0;
    }
    x = (UnkStruct_skyRasterExt *)((char *)tex->field_0 + skyRasterExt);
    v = (l < 4) ? l : 3;
    x->field_14 = (unsigned short)((x->field_14 & 0xCFFF) | (v << 12));
    return tex;
}

// [C] a texture's K: ext halfword +0x14 bits 0-11 sign-extended * 0.0625, or 0.0 when the texture or raster is NULL
// 0x001FF6E0
extern "C" float func_001FF6E0(UnkStruct_skyTexture *tex)
{
    UnkStruct_skyRasterExt *x;
    int v;

    if (tex == 0 || tex->field_0 == 0) {
        return 0.0f;
    }
    x = (UnkStruct_skyRasterExt *)((char *)tex->field_0 + skyRasterExt);
    v = x->field_14 & 0xFFF;
    if ((v & 0x800) != 0) {
        v = v | -0x1000;
    }
    return (float)v * 0.0625f;
}

// [C] a texture's L: ext halfword +0x14 bits 12-13, or 0 when the texture or raster is NULL
// 0x001FF738
extern "C" unsigned int func_001FF738(UnkStruct_skyTexture *tex)
{
    UnkStruct_skyRasterExt *x;

    if (tex == 0 || tex->field_0 == 0) {
        return 0;
    }
    x = (UnkStruct_skyRasterExt *)((char *)tex->field_0 + skyRasterExt);
    return (x->field_14 & 0x3000) >> 12;
}

// 0x001FF768
extern "C" int RpSkySelectTrueTSClipper(int mode)
{
    skyTLClipperMode = mode;
    return 1;
}

// 0x001FF778
extern "C" int func_001FF778(void)
{
    return skyTLClipperMode;
}

// 0x001FF780
extern "C" int func_001FF780(int v)
{
    D_004E18E4 = v;
    return 1;
}

// 0x001FF790
extern "C" int func_001FF790(void)
{
    return D_004E18E4;
}

// [C] jtbl_004B8F40 over states 1..5 (others return 0): 1 the dither state (0 returned, nothing written, when the
// draw depth is 32 or the engine is stopped), 2 ALPHA_1's A..D byte with FIX (bits 32-39) moved to bits 24-31, 3
// TEST_1's low word, 4 0 when D_004E28B0 is set else the float D_004E28B4, 5 skyMaxMipLevels. Returns 1 when written.
// 0x001FF798
extern "C" int RpSkyRenderStateGet(int state, void *param)
{
    switch (state) {
    case 1:
        if (skyVideoMode->field_0.field_8 == 0x20) {
            return 0;
        }
        if (engineStarted == 0) {
            return 0;
        }
        *(int *)param = skyDitherState;
        return 1;
    case 2:
        {
            unsigned long long a = skyAlpha_1;

            *(int *)param = (int)((a & 0xFF) | ((unsigned long long)((long long)a >> 8) & 0xFF000000ULL));
        }
        return 1;
    case 3:
        *(int *)param = (int)(unsigned int)skyTest_1;
        return 1;
    case 4:
        if (D_004E28B0 != 0) {
            *(int *)param = 0;
        } else {
            *(float *)param = D_004E28B4;
        }
        return 1;
    case 5:
        *(int *)param = skyMaxMipLevels;
        return 1;
    default:
        return 0;
    }
}

// [C] no static reference (an unreferenced getter)
// 0x001FF848
extern "C" UnkStruct_skyRaster *func_001FF848(void)
{
    return (UnkStruct_skyRaster *)skyDisplayRaster;
}

// [C] no static reference (an unreferenced getter)
// 0x001FF850
extern "C" UnkStruct_skyRaster *func_001FF850(void)
{
    return (UnkStruct_skyRaster *)skyDrawRaster;
}

// ---------------------------------------------------------------------------------------------------------------
// boot2 additions (track rw): RpSkyRenderStateSet and the small func_001FF9E0.

// [C] a qword of GIF data in the scratchpad packet (words in memory order).
struct UnkStruct_skyQword {
    unsigned int field_0;
    unsigned int field_4;
    unsigned int field_8;
    unsigned int field_C;
};

extern "C" int _rwDMAOpenVIFPkt(unsigned int type, unsigned int nQW);

// [C] states 1..5 (jtbl_004B8620; anything else returns 0): 1 dither matrix, 2 alpha, 3 test, 4 a float stored with
// D_004E28B0 = 0 (D_004E28B4 = *(float *)value), 5 the mip level count (*(int *)value in 1..7 -> skyMaxMipLevels,
// else 0). State 1 needs the engine started (engineStarted, else 0). States 1..3 open a 2-qword VIF packet of
// type bit 31 (_rwDMAOpenVIFPkt(0x80000000, 2), 0 -> return 0), write the GIFtag {NLOOP 1, EOP, NREG 1, A+D} then one
// A+D qword {data, register}: state 1 value 0 -> DIMX 0 (skyDitherState = 0), 0x55555555 -> DIMX 0x1212303012123030
// (skyDitherState = value), else DIMX 0x6071243571603524 (skyDitherState = 1), register 0x44; state 2 -> ALPHA_1 (0x42) =
// (value & 0xFF) | (value >> 24) << 32, saved in skyAlpha_1; state 3 -> TEST_1 (0x47) = (skyTest_1 & ~0xFFFF) |
// (value & 0xFFFF), saved in skyTest_1. The cursor (farFogPlane) advances 0x10 per qword. Returns 1 on success.
// 0x001F5510
extern "C" int RpSkyRenderStateSet(int state, unsigned int value)
{
    UnkStruct_skyQword *q;
    unsigned long long data;
    unsigned long long reg;
    unsigned int cur;

    switch (state) {
    case 1:
    case 2:
    case 3:
        if (state == 1 && engineStarted == 0) {   // [C] only state 1 tests engineStarted (0x1F5548)
            return 0;
        }
        break;
    case 4:
        {
            float f = *(float *)value;

            D_004E28B0 = 0;
            D_004E28B4 = f;
            return 1;
        }
    case 5:
        {
            int v = *(int *)value;

            if ((unsigned int)(v - 1) < 7) {
                skyMaxMipLevels = v;
                return 1;
            }
            return 0;
        }
    default:
        return 0;
    }
    if (!_rwDMAOpenVIFPkt(0x80000000, 2)) {
        return 0;
    }
    q = (UnkStruct_skyQword *)farFogPlane;
    q->field_0 = 0x8001;
    q->field_4 = 0x10000000;
    q->field_8 = 0xE;
    q->field_C = 0;
    cur = farFogPlane + 0x10;
    farFogPlane = cur;
    if (state == 1) {
        reg = 0x44;
        if (value == 0) {
            data = 0;
            skyDitherState = 0;
        } else if (value == 0x55555555) {
            data = 0x1212303012123030ULL;
            skyDitherState = (int)value;
        } else {
            data = 0x6071243571603524ULL;
            skyDitherState = 1;
        }
        cur = farFogPlane;
    } else if (state == 2) {
        reg = 0x42;
        data = (unsigned long long)(value & 0xFF) | ((unsigned long long)(value >> 24) << 32);
        skyAlpha_1 = data;
    } else {
        reg = 0x47;
        data = (skyTest_1 & 0xFFFFFFFFFFFF0000ULL) | (unsigned long long)(value & 0xFFFF);
        skyTest_1 = data;
    }
    q = (UnkStruct_skyQword *)cur;
    q->field_0 = (unsigned int)data;
    q->field_4 = (unsigned int)(data >> 32);
    q->field_8 = (unsigned int)reg;
    q->field_C = 0;
    cur = cur + 0x10;
    farFogPlane = cur;
    return 1;
}

// [C] 4 when the pointer is not NULL and the word it points to is not zero, else 0.
// 0x001FF9E0
extern "C" int func_001FF9E0(int *p)
{
    int r = 0;

    if (p != 0) {
        r = 4;
        if (*p == 0) {
            r = 0;
        }
    }
    return r;
}
