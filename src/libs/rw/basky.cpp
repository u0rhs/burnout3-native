#include "rw/skyglobals.h"
#include "b3hw/hw.h"

// RenderWare 3.6 sky2 driver: the device and its system function (RW 3.1 file name: driver/sky2/basky.c; reading aid
// only). Written from the retail asm (decomp assembly/asm/rw/renderware.s). Tags: [C] confirmed from the asm, [L], [G].
// boot1 staging (rw_wip); boot2: SkyOpen / SkyStart (spec: docs/results/boot1-2026-11-05/rw/sky2_handlers.md).

// [C] 0x38 bytes (symbol table), copied whole by request 4 ([L] RwDevice). Image values from renderware.data.s.
struct UnkStruct_dGSkyDevice {
    float field_0;                                  // [C] 1.0 ([L] gammaCorrection)
    int (*field_4)(int, void *, void *, int);       // [C] SkySystem ([L] fpSystem)
    float field_8;                                  // [C] 65535.0 (0x477FFF00) ([L] zBufferNear)
    float field_C;                                  // [C] 0.0 ([L] zBufferFar)
    int (*field_10)(int, void *);                   // [C] _rwSkySetRenderState ([L] fpRenderStateSet)
    int (*field_14)(int, void *);                   // [C] SkyGetRenderState ([L] fpRenderStateGet)
    void *field_18[8];                              // [C] 0 in the image ([L] the Im2D / Im3D render functions)
};
// [C] the device is a function-static: the table names it dGSkyDevice.311 (0x0049AD38), not a C identifier.
extern UnkStruct_dGSkyDevice D_0049AD38;

// [C] one 0x18-byte entry of videoModes (0x858 bytes = 0x59 entries) ([L] RwVideoMode).
struct UnkStruct_videoModes {
    int field_0;            // [L] width
    int field_4;            // [L] height
    int field_8;            // [C] 0x20 gives camera format 0x500, anything else 0x100 ([L] depth)
    unsigned int field_C;   // [L] flags
    int field_10;           // [L] refRate
    int field_14;           // [L] format
};
// [C] 0x34 bytes (symbol table); mode -1. The game fills it (SetVideoModeParams__18CB3GraphicsManagerFP14RwSkyVideoMode
// through RpSkyGetModeStructurePtr) and SkyStart completes it ([L] RpSkyVideoMode).
struct UnkStruct_customMode {
    UnkStruct_videoModes field_0;   // [C] what request 6 returns for mode -1 ([L] the draw buffer's mode)
    UnkStruct_videoModes field_18;  // [C] SkyStart's display side ([L] the display buffer's mode)
    unsigned char field_30;         // [L] numDrawBuffers
    unsigned char field_31;         // [L] numDisplayBuffers
    unsigned char field_32;         // [C] the GS output mode for sceGsResetGraph (2 NTSC, 3 PAL, 0x50 480p)
    unsigned char field_33;         // [L] inplace (draw and display share the buffers)
};

// [C] _rwDMAFlipData: 0x3D0 bytes (symbol table): an sceGsDBuffDc (disp[2] +0x00 / +0x28, giftag0 +0x50, draw01 +0x60,
// draw02 +0xE0, clear0 +0x160, giftag1 +0x1C0, draw11 +0x1D0, draw12 +0x250, clear1 +0x2D0 [C SkyStart's offsets, L SDK
// libgraph.h]) and RW's words from +0x330; SkyStart reaches it as 64-bit words, q[offset / 8].
struct UnkStruct_rwDMAFlipData {
    unsigned long long q[0x78];
    int field_3C0;   // [C] cleared by the stop request and by SkyStart; set by PresentDrawBuffer__18CB3GraphicsManagerFv
    int field_3C4;
    int field_3C8;
    int field_3CC;
};

// [C] request 0xB's out: an array of numIn function slots ([L] RwStandardFunc stdFunc[], ourGlobals + 0x48).
struct UnkStruct_SkyStandards {
    int (*field_0)(void *, void *, int);
};

// [C] skyProj: 0x40 bytes (symbol table) ([L] RwMatrix: right, flags at +0xC, up, pad, at, pad, pos, pad).
struct UnkStruct_skyProj {
    float field_0;
    float field_4;
    float field_8;
    unsigned int field_C;   // [C] ORed with 0x20003 ([L] rwMATRIXTYPEORTHONORMAL | rwMATRIXINTERNALIDENTITY)
    float field_10;
    float field_14;
    float field_18;
    unsigned int pad_1C;
    float field_20;
    float field_24;
    float field_28;
    unsigned int pad_2C;
    float field_30;
    float field_34;
    float field_38;
    unsigned int pad_3C;
};

extern UnkStruct_videoModes videoModes[];          // [C] 0x0049A370, 0x59 entries
extern UnkStruct_customMode customMode;            // [C] 0x00675F10
extern UnkStruct_rwDMAFlipData _rwDMAFlipData;     // [C] 0x006754C0
extern UnkStruct_skyProj skyProj;                  // [C] 0x00675E00
extern int engineStarted;                          // [C] gp-relative; 1 after SkyStart, 0 after the stop request
extern int currentMode;                            // [C] gp-relative; 0x58 after SkyOpen
extern int currentCameraFormat;                    // [C] gp-relative ([L] the camera raster format)
extern unsigned int textureBankStart;              // [C] gp-relative; set by SkyStart (word address of texture VRAM)
extern void *rwDMASavedGP;                         // [C] gp-relative; request 4's inOut ([L] 3.1 dgGGlobals.memFuncs)
extern int needCache;                              // [C] gp-relative
extern int cacheOpened;                            // [C] gp-relative

// [C] hardware-level handlers (HANDLERS.md); they read none of a0..a3 (SkySystem's jr leaves the jump target in a0).
extern "C" int SkyOpen(void);
extern "C" int SkyStart(void);
extern "C" void _rwDMADmaClose(void);
// [C] other driver functions, ported elsewhere in rw_wip or reached through them
extern "C" void skyTexCacheCloseCache(void);
extern "C" void _rwSkyRasterInitiateStop(void);
extern "C" void _rwSkyUnOverloadPipelines(void);
extern "C" void _rwSkyOverloadPipelines(void);
extern "C" int _rwSkyRasterFinaliseStart(void);
extern "C" int _rpSkyCreateFrameBufferRasters(void);
extern "C" int RwRasterDestroy(void *raster);
// [C] RW core: they store the function in the matrix / vector module's globals (0 picks the default)
typedef void (*UnkFn_001DAB10)(void *, const void *, const void *);                          // as bamatrix.cpp
typedef void *(*UnkFn_001DE550_3)(void *, const void *, const void *);                       // as bavector.cpp
typedef void *(*UnkFn_001DE550_4)(void *, const void *, int, const void *);                  // as bavector.cpp
extern "C" int _rwMatrixSetMultFn(UnkFn_001DAB10 multFn);
extern "C" int _rwVectorSetMultFn(UnkFn_001DE550_3 multPoint, UnkFn_001DE550_4 multPoints,
                                  UnkFn_001DE550_3 multVector, UnkFn_001DE550_4 multVectors);
extern "C" void func_001FFE18(void *, const void *, const void *);
extern "C" float *func_001FFE90(float *, const float *, const float *);
extern "C" void *func_001FFEF0(void *, const void *, int, const void *);
extern "C" float *func_001FFF80(float *, const float *, const float *);
extern "C" void *func_001FFFD8(void *, const void *, int, const void *);

// [C] the standards: func_001FFE10 returns 0 ([L] SkyStandardNull), func_001FFE08 returns 1; the rest are the driver's
// raster / camera / native texture functions (saGStandards order). Only their addresses are stored.
extern "C" int func_001FFE10(void *, void *, int);
extern "C" int SkyCameraBeginUpdate(void *, void *);
extern "C" int SkyCameraEndUpdate(void *, void *);
extern "C" int SkyCameraClear(void *, void *, int);
extern "C" int SkyRasterShowRaster(void *, void *, int);
extern "C" int func_00207E30(void *, void *, int);
extern "C" int func_00207F28(void *, void *, int);
extern "C" int _rwSkyRasterSetImage(void *, void *, int);
extern "C" int _rwSkyImageGetRaster(void *, void *, int);
extern "C" int SkyRasterDestroy(unsigned int, void *, int);
extern "C" int SkyRasterCreate(unsigned int, void *, int);
extern "C" int func_001F9DC0(void *, void *, int);
extern "C" int _rwSkyImageFindRasterFormat(void *, void *, int);
extern "C" int func_001FFB50(void *, void *, int);
extern "C" int SkyRasterLock(void *, void *, int);
extern "C" int SkyRasterUnlock(void *, void *, int);
extern "C" int SkyRasterLockPalette(void *, void *, int);
extern "C" int SkyRasterUnlockPalette(void *, void *, int);
extern "C" int _rwSkyRasterClear(void *, void *, int);
extern "C" int _rwSkyRasterClearRect(void *, void *, int);
extern "C" int _rwSkyRasterRender(void *, void *, int);
extern "C" int _rwSkyRasterRenderScaled(void *, void *, int);
extern "C" int _rwSkyRasterRenderFast(void *, void *, int);
extern "C" int func_00205E30(void *, void *, int);
extern "C" int func_001FFE08(void *, void *, int);
extern "C" int SkyNativeTextureGetSize(void *, void *, int);
extern "C" int SkyNativeTextureWrite(void *, void *, int);
extern "C" int SkyNativeTextureRead(void *, void *, int);
extern "C" int func_001FFD18(void *, void *, int);

// [C] the fog VIF packets: 0xE0 bytes each (symbol sizes). +0x00 DMAtag END + STCYCL/STMOD, +0x10 STMASK + UNPACK
// (0x30 halfwords from +0x1C), +0x70 UNPACK (0x30 halfwords from +0x80) ([L] fog value per depth step).
struct UnkStruct_skyFog {
    unsigned long long field_0;     // [C] 0x00000000F000000D (DMAtag END, QWC 0xD, IRQ)
    unsigned long long field_8;     // [C] 0x0500000001000102 (VIF STCYCL CL 2 WL 1, STMOD 0)
    unsigned long long field_10;    // [C] 0x3F3F3F3F20000000 (VIF STMASK + the mask)
    unsigned long long field_18;    // [C] 0x71304001 (UNPACK; word +0x1C is then the table's)
    unsigned char pad_20[0x50];
    unsigned long long field_70;    // [C] 0 (+0x70..+0x7B are then the table's)
    unsigned long long field_78;    // [C] 0x7130420200000000 (UNPACK at word +0x7C)
    unsigned char pad_80[0x60];
};
extern UnkStruct_skyFog skyLinearFog;       // [C] 0x00675F90
extern UnkStruct_skyFog skyExpFog;          // [C] 0x00676070
extern UnkStruct_skyFog skyExp2Fog;         // [C] 0x00676150
extern UnkStruct_skyFog skyMungedUserFog;   // [C] 0x00676230
extern float D_004E0728;                    // [C] 2.6842 (the linear step)
extern float D_004E072C;                    // [C] 1/95
extern float D_004E0730;                    // [C] 1/95^2
extern "C" float func_0011E388(float x, float y);   // [C] retail newlib powf (a tail jump to __ieee754_powf)
extern "C" void SyncDCache(void *start, void *end);

// [C] the three header qwords of every fog packet, then 96 values per table into the halfword slots (step i = 0..0x5F
// fills slot s = 0x5F - i: even s at +0x1C + s, odd s at +0x80 + s - 1): linear 255 - i * D_004E0728, exp 255 /
// 256^(i * D_004E072C), exp2 255 / 256^(i * (i * D_004E0730)), each stored as (int)v << 4 (truncating). The four
// packets are synced over 0x160 bytes; then fogTable, currentMode 0x58 (videoModes' last entry), engineStarted and
// the camera format from that mode's depth. Returns 1.
// [C 0x1FC67C..0x1FC688] retail first sets COP0 Status.CU2 (COP2 usable): no guest-visible state; the host's VU0 is
// always usable, so it has no C here.
// 0x001FC638
extern "C" int SkyOpen(void)
{
    int i;
    int slot;
    float step;
    float v;

    skyLinearFog.field_0 = 0xF000000DULL;
    skyLinearFog.field_8 = 0x0500000001000102ULL;
    skyMungedUserFog.field_0 = 0xF000000DULL;
    skyMungedUserFog.field_8 = 0x0500000001000102ULL;
    skyExp2Fog.field_0 = 0xF000000DULL;
    skyExp2Fog.field_8 = 0x0500000001000102ULL;
    skyExpFog.field_0 = 0xF000000DULL;
    skyExpFog.field_8 = 0x0500000001000102ULL;
    skyLinearFog.field_10 = 0x3F3F3F3F20000000ULL;
    skyLinearFog.field_18 = 0x71304001ULL;
    skyMungedUserFog.field_10 = 0x3F3F3F3F20000000ULL;
    skyMungedUserFog.field_18 = 0x71304001ULL;
    skyExp2Fog.field_10 = 0x3F3F3F3F20000000ULL;
    skyExp2Fog.field_18 = 0x71304001ULL;
    skyExpFog.field_10 = 0x3F3F3F3F20000000ULL;
    skyExpFog.field_18 = 0x71304001ULL;
    skyMungedUserFog.field_70 = 0;
    skyMungedUserFog.field_78 = 0x7130420200000000ULL;
    skyLinearFog.field_70 = 0;
    skyLinearFog.field_78 = 0x7130420200000000ULL;
    skyExp2Fog.field_70 = 0;
    skyExp2Fog.field_78 = 0x7130420200000000ULL;
    skyExpFog.field_70 = 0;
    skyExpFog.field_78 = 0x7130420200000000ULL;

    for (i = 0; i < 0x60; i++) {
        slot = 0x5F - i;
        int base = (slot & 1) ? 0x80 : 0x1C;
        int off = (slot >> 1) << 1;
        step = (float)i;
        v = 255.0f - step * D_004E0728;
        *(short *)((char *)&skyLinearFog + base + off) = (short)((int)v << 4);
        v = 255.0f / func_0011E388(256.0f, step * D_004E072C);
        *(short *)((char *)&skyExpFog + base + off) = (short)((int)v << 4);
        v = 255.0f / func_0011E388(256.0f, step * (step * D_004E0730));
        *(short *)((char *)&skyExp2Fog + base + off) = (short)((int)v << 4);
    }

    SyncDCache(&skyLinearFog, (char *)&skyLinearFog + 0x15F);
    SyncDCache(&skyExpFog, (char *)&skyExpFog + 0x15F);
    SyncDCache(&skyExp2Fog, (char *)&skyExp2Fog + 0x15F);
    SyncDCache(&skyMungedUserFog, (char *)&skyMungedUserFog + 0x15F);
    fogTable = &skyLinearFog;
    currentMode = 0x58;
    engineStarted = 0;
    if (videoModes[0x58].field_8 == 0x20) {
        currentCameraFormat = 0x500;
    } else {
        currentCameraFormat = 0x100;
    }
    return 1;
}

extern int zBufferDepth;                    // [C] gp 0x004E18A4 (RpSkySelectDeepZBuffer: 16 or 32)

// [C] A64 / A32 rounding of the buffer sizes skyCustomModeIsOk checks: bytes of a w x h buffer of `depth` bits
static unsigned int skyModeBytes(int w, int h, int depth)
{
    unsigned int n;

    if (depth == 16) {
        n = (unsigned int)((w + 0x3F) & -0x40) * (unsigned int)((h + 0x3F) & -0x40);
        return n << 1;
    }
    n = (unsigned int)((w + 0x3F) & -0x40) * (unsigned int)((h + 0x1F) & -0x20);
    return n << 2;
}

// [C] the custom mode's checks, in the asm's order (the first failure returns 0): the output mode from the display
// height when 0 (224 / 448 NTSC 2, 256 / 512 PAL 3, 480 480p 0x50, else fail), one of 2 / 3 / 0x50; inplace needs two
// draw and two display buffers; 1 or 2 of each; depths 16 or 32; inplace needs the draw buffer at least as big as the
// display one; a zero raster format becomes 0x100 (16-bit) / 0x500 (32-bit), a nonzero one must be that; all buffers
// (inplace: draw and Z only) fit in 4 MB of VRAM. The fills persist when a later check fails.
// 0x001FD138
extern "C" int skyCustomModeIsOk(void)
{
    UnkStruct_customMode *m = &customMode;
    int h;
    unsigned int drawBytes;
    unsigned int dispBytes;
    unsigned int zBytes;

    if (m->field_32 == 0) {
        h = m->field_18.field_4;
        if (h == 0x1E0) {
            m->field_32 = 0x50;
        } else if (h == 0x100 || h == 0x200) {
            m->field_32 = 3;
        } else if (h == 0xE0 || h == 0x1C0) {
            m->field_32 = 2;
        } else {
            return 0;
        }
    }
    if (m->field_32 != 2 && m->field_32 != 0x50 && m->field_32 != 3) {
        return 0;
    }
    if (m->field_33 != 0 && *(unsigned short *)&m->field_30 != 0x202) {
        return 0;
    }
    if ((unsigned int)(m->field_30 - 1) >= 2) {
        return 0;
    }
    if ((unsigned int)(m->field_31 - 1) >= 2) {
        return 0;
    }
    if (m->field_0.field_8 != 0x10 && m->field_0.field_8 != 0x20) {
        return 0;
    }
    if (m->field_18.field_8 != 0x10 && m->field_18.field_8 != 0x20) {
        return 0;
    }
    drawBytes = skyModeBytes(m->field_0.field_0, m->field_0.field_4, m->field_0.field_8);
    dispBytes = skyModeBytes(m->field_18.field_0, m->field_18.field_4, m->field_18.field_8);
    zBytes = skyModeBytes(m->field_0.field_0, m->field_0.field_4, zBufferDepth);
    if (m->field_33 != 0 && drawBytes < dispBytes) {
        return 0;
    }
    if (m->field_0.field_14 != 0) {
        if (m->field_0.field_8 == 0x10 && m->field_0.field_14 != 0x100) {
            return 0;
        }
        if (m->field_0.field_8 == 0x20 && m->field_0.field_14 != 0x500) {
            return 0;
        }
    } else {
        m->field_0.field_14 = (m->field_0.field_8 == 0x10) ? 0x100 : 0x500;
    }
    if (m->field_18.field_14 != 0) {
        if (m->field_18.field_8 == 0x10 && m->field_18.field_14 != 0x100) {
            return 0;
        }
        if (m->field_18.field_8 == 0x20 && m->field_18.field_14 != 0x500) {
            return 0;
        }
    } else {
        m->field_18.field_14 = (m->field_18.field_8 == 0x10) ? 0x100 : 0x500;
    }
    if (m->field_33 != 0) {
        return 0x400000 < drawBytes * m->field_30 + zBytes ? 0 : 1;
    }
    return 0x400000 < dispBytes * m->field_31 + drawBytes * m->field_30 + zBytes ? 0 : 1;
}

// [C] one qword at the packet cursor (the asm's sq of a (lo, hi) register pair), the cursor advanced
struct UnkStruct_skyQword {
    unsigned long long field_0;
    unsigned long long field_8;
};
extern int skyBlendStates[];                // [C] 0x0049ABC8, 0x90 bytes; word +0x50 = 0x44 (ALPHA_1 for src / dest 2 / 3)
extern int skySrcBlend;                     // [C] gp 0x004E1880
extern int skyDestBlend;                    // [C] gp 0x004E1884
extern unsigned long long skyTex1_1;        // [C] gp 0x004E1860, 8 bytes
extern unsigned long long skyAlpha_1;       // [C] gp 0x004E1868, 8 bytes
extern unsigned long long skyPrim_State;    // [C] gp 0x004E1878, 8 bytes
extern unsigned long long skyClamp_1;       // [C] gp 0x004E1858, 8 bytes
extern int gSkyCullState;                   // [C] gp 0x004E286C
extern int skyDitherState;                  // [C] gp 0x004E2884
extern int skyVertexAlpha;                  // [C] gp 0x004E2868
extern int skyAlphaTex;                     // [C] gp 0x004E2864
extern "C" int _rwDMAOpenVIFPkt(unsigned int type, int nQW);
extern "C" int _rwSkySetRenderState(int state, unsigned int value);   // [C] a1 is a raw value (basky_state.cpp)

// [C] the driver's render state at start-up: the shadows, then a 7-qword GIF packet (ALPHA_1, CLAMP_1, PABE, TEXA,
// DIMX, COLCLAMP), then Z test and Z write on through _rwSkySetRenderState (6 and 8, value 1).
// 0x001FC4C0
extern "C" void setInitialRenderState(void)
{
    unsigned long long alpha = (unsigned long long)(long long)skyBlendStates[0x50 / 4];
    UnkStruct_skyQword *p;

    skyDestBlend = 3;
    skyTex1_1 = 0x60;
    gSkyCullState = 2;
    skyDitherState = 1;
    skySrcBlend = 2;
    skyAlpha_1 = alpha;
    skyPrim_State = 8;
    skyVertexAlpha = 0;
    skyAlphaTex = 0;
    skyTextureRaster = 0;
    skyClamp_1 = 0;
    _rwDMAOpenVIFPkt(0x80000000, 7);
    p = (UnkStruct_skyQword *)farFogPlane;
    p[0].field_0 = 0x1000000000008006ULL;   // GIFtag NLOOP 6, EOP, PACKED, NREG 1
    p[0].field_8 = 0xE;                     // A+D
    farFogPlane = (RwUInt32)(p + 1);
    p = p + 1;
    p[0].field_0 = alpha;
    p[0].field_8 = 0x42;                    // ALPHA_1
    farFogPlane = (RwUInt32)(p + 1);
    p[1].field_0 = 0;
    p[1].field_8 = 0x08;                    // CLAMP_1
    farFogPlane = (RwUInt32)(p + 2);
    p[2].field_0 = 0;
    p[2].field_8 = 0x49;                    // PABE
    farFogPlane = (RwUInt32)(p + 3);
    p[3].field_0 = 0x0000008000000000ULL;   // TEXA: TA1 0x80
    p[3].field_8 = 0x3B;
    farFogPlane = (RwUInt32)(p + 4);
    p[4].field_0 = 0x6071243571603524ULL;   // DIMX
    p[4].field_8 = 0x44;
    farFogPlane = (RwUInt32)(p + 5);
    p[5].field_0 = 1;                       // COLCLAMP
    p[5].field_8 = 0x46;
    farFogPlane = (RwUInt32)(p + 6);
    _rwSkySetRenderState(6, 1);
    _rwSkySetRenderState(8, 1);
}

// [C] skyInitialClear: 0xD0 bytes (.bss): a GIF PACKED packet, one tag and 12 A+D qwords (data, register).
struct UnkStruct_skyInitialClear {
    unsigned long long field_0;     // [C] GIFtag low
    unsigned long long field_8;     // [C] GIFtag REGS
    unsigned long long field_10[24];
};
extern UnkStruct_skyInitialClear skyInitialClear;   // [C] 0x00675E40
extern int skyFSAA0VisibleWidth;            // [C] gp 0x004E18A8
extern UnkStruct_customMode *skyVideoMode;  // [C] gp 0x004E1808
extern int skyFrameBit;                     // [C] gp 0x004E188C
extern int skyTexCacheRestoreRequired;      // [C] gp 0x004E18B4
extern int skySuspended;                    // [C] gp 0x004E182C
extern unsigned long long skyXOff;          // [C] gp 0x004E1890, 8 bytes
extern unsigned long long skyYOff;          // [C] gp 0x004E1898, 8 bytes
extern unsigned long long skyFrame_1;       // [C] gp 0x004E1838: FRAME_1 shadow (read by ~30 game functions)
extern unsigned long long skyZbuf_1;        // [C] gp 0x004E1830: ZBUF_1 shadow
extern unsigned long long skyTest_1;        // [C] gp 0x004E1840: TEST_1 shadow
extern unsigned long long skyXyoffset_1;    // [C] gp 0x004E1848: XYOFFSET_1 shadow
extern int D_004E28B0;                      // [C] gp-relative; 1 after SkyStart (RpSkyRenderStateGet / Set)
extern unsigned char D_004B1400[];          // [C] a DMA chain in .data (STCYCL / STMASK / STMOD, TEST_1, TEXA, ALPHA_1)
extern "C" int skyCustomModeIsOk(void);
extern "C" void _rwSkyInitConfigSystem(void);
extern "C" int skyTexCacheOpenCache(unsigned int start, unsigned int size);
extern "C" int _rwDMADmaOpen(void);
extern "C" void setInitialRenderState(void);
extern "C" void setUpFSAA0(UnkStruct_rwDMAFlipData *flip);
// [C] Sony libgraph / libdma (src/libs/sce)
extern "C" int sceGsSyncV(int mode);
extern "C" void sceGsResetGraph(short mode, short inter, short omode, short ffmode);
extern "C" unsigned int sceDmaGetChan(int id);   // [C] a channel register block's address (src/libs/sce/libdma.cpp)
extern "C" void sceDmaSendN(unsigned int chan, unsigned int addr, int qwc);
extern "C" void sceDmaSend(unsigned int chan, unsigned int tag);
extern "C" int sceGsSyncPath(int mode, unsigned short timeout);
extern "C" void sceGsDefDispEnv(void *disp, short psm, short w, short h, short dx, short dy);
extern "C" int sceGsSetDefDrawEnv(void *draw, short psm, short w, short h, short ztest, short zpsm);
extern "C" int sceGsSetDefDrawEnv2(void *draw, short psm, short w, short h, short ztest, short zpsm);
extern "C" int sceGsSwapDBuffDc(void *db, int id);

// [C] the frame / Z buffer size in 2048-word GS pages: width rounded up to 64, height to 64 (16-bit) or 32 (32-bit),
// halved for 16-bit; unsigned 32-bit as the asm's mult / srl.
static unsigned int skyStartPages(int w, int h, int depth)
{
    unsigned int n;

    if (depth == 16) {
        n = (unsigned int)((w + 0x3F) & -0x40) * (unsigned int)((h + 0x3F) & -0x40);
        n = n >> 1;
    } else {
        n = (unsigned int)((w + 0x3F) & -0x40) * (unsigned int)((h + 0x1F) & -0x20);
    }
    return n >> 11;
}

// [C] the low 9 bits (FBP / ZBP) of a 64-bit GS register value replaced
static unsigned long long skyStartSetBase(unsigned long long reg, unsigned int base)
{
    return (reg & ~0x1FFULL) | (base & 0x1FF);
}

// [C] the engine start (request 2): the initial-clear packet, the GS / DMA reset, customMode from currentMode, the
// double-buffer description _rwDMAFlipData (display / draw environments, frame and Z buffer pages), the texture cache,
// the first flip, the DMA layer, the GS register shadows and the initial render state. Returns 0 when the mode is not
// usable or the DMA layer does not open, else _rwDMADmaOpen's result. Every step in the asm's order
// (docs/results/boot1-2026-11-05/rw/sky2_handlers.md, "SkyStart").
// 0x001FD490
extern "C" int SkyStart(void)
{
    UnkStruct_customMode *m;
    UnkStruct_rwDMAFlipData *flip;
    unsigned long long *q;
    unsigned int f2;
    unsigned int t;
    unsigned int u;
    int r;
    short psm;
    short zpsm;

    // 1. [C 0x1FD494..0x1FD774] the GIF tag (NLOOP 1, EOP, PACKED, NREG 12, all A+D; the REGS word keeps its top 16
    // bits), then the 12 (data, register) pairs: a 1024x1024 sprite clear of VRAM from 0
    skyInitialClear.field_8 = (skyInitialClear.field_8 & 0xFFFF000000000000ULL) | 0x0000EEEEEEEEEEEEULL;
    skyInitialClear.field_0 = 0xC000000000008001ULL;
    skyInitialClear.field_10[0x1] = 0x42;                   // ALPHA_1 = 0
    skyInitialClear.field_10[0x2] = 0x100000;               // FRAME_1: FBW 16
    skyInitialClear.field_10[0x3] = 0x4C;
    skyInitialClear.field_10[0x5] = 0x1A;                   // PRMODECONT = 1
    skyInitialClear.field_10[0x6] = 0x03FF000003FF0000ULL;  // SCISSOR_1
    skyInitialClear.field_10[0x7] = 0x40;
    skyInitialClear.field_10[0x8] = 0x30000;                // TEST_1: ZTE, ZTST ALWAYS
    skyInitialClear.field_10[0x9] = 0x47;
    skyInitialClear.field_10[0xB] = 0x18;                   // XYOFFSET_1 = 0
    skyInitialClear.field_10[0xC] = 0x10A000100ULL;         // ZBUF_1: ZBP 0x100, PSM 0xA, ZMSK
    skyInitialClear.field_10[0xD] = 0x4E;
    skyInitialClear.field_10[0xE] = 6;                      // PRIM = sprite
    skyInitialClear.field_10[0x15] = 1;
    skyInitialClear.field_10[0x16] = 0x3FFF3FFF;            // XYZ2
    skyInitialClear.field_10[0x17] = 5;
    skyInitialClear.field_10[0x0] = 0;
    skyInitialClear.field_10[0x4] = 1;
    skyInitialClear.field_10[0xA] = 0;
    skyInitialClear.field_10[0xF] = 0;                      // PRIM's register 0x00
    skyInitialClear.field_10[0x10] = 0;                     // RGBAQ = 0
    skyInitialClear.field_10[0x11] = 1;
    skyInitialClear.field_10[0x12] = 0;                     // XYZ2 = 0
    skyInitialClear.field_10[0x13] = 5;
    skyInitialClear.field_10[0x14] = 0;                     // RGBAQ = 0
    SyncDCache(&skyInitialClear, (char *)&skyInitialClear + 0x14F);

    // 2.
    _rwSkyInitConfigSystem();
    sceGsSyncV(0);

    // 3. [C 0x1FD788..0x1FD878] customMode from the current mode (mode -1: as the game filled it)
    m = &customMode;
    if (currentMode != -1) {
        m->field_0 = videoModes[currentMode];
        m->field_18 = videoModes[currentMode];
        if ((m->field_18.field_C & 0x100) != 0) {
            m->field_18.field_4 = m->field_18.field_4 >> 1;
            m->field_31 = 1;
            m->field_30 = 1;
            if (skyFSAA0VisibleWidth != 0) {
                m->field_18.field_0 = skyFSAA0VisibleWidth;
            } else {
                skyFSAA0VisibleWidth = m->field_18.field_0;
            }
            m->field_33 = 0;
        } else {
            m->field_33 = 1;
            m->field_31 = 2;
            m->field_30 = 2;
        }
        m->field_32 = 0;
    }

    // 4.
    if (skyCustomModeIsOk() == 0) {
        return 0;
    }
    skyVideoMode = m;

    // 5. [C 0x1FD894..0x1FD968] GS reset for the output mode, the initial clear on the GIF channel
    f2 = m->field_18.field_C;
    if (m->field_32 == 0x50) {
        sceGsResetGraph(0, 0, 0x50, 1);
    } else if ((f2 & 4) != 0) {
        sceGsResetGraph(0, 1, m->field_32, 1);
    } else if ((f2 & 2) != 0) {
        sceGsResetGraph(0, 1, m->field_32, 0);
    } else if ((f2 & 0x100) != 0) {
        sceGsResetGraph(0, 1, m->field_32, 1);
    } else {
        sceGsResetGraph(0, 0, m->field_32, 1);
    }
    sceDmaSendN(sceDmaGetChan(2), (unsigned int)&skyInitialClear, 0xD);
    while (sceGsSyncPath(1, 0) != 0) {
    }
    b3hw_write32(0x1000E010, 4);   // [C] D_STAT: clear channel 2's interrupt status

    // 6. [C 0x1FD978..0x1FDA9C] the display and draw environments
    flip = &_rwDMAFlipData;
    q = flip->q;
    psm = (m->field_18.field_8 == 0x10) ? 0xA : 0;
    sceGsDefDispEnv(&q[0x00 / 8], psm, (short)m->field_18.field_0, (short)m->field_18.field_4, 0, 0);
    psm = (m->field_18.field_8 == 0x10) ? 0xA : 0;
    sceGsDefDispEnv(&q[0x28 / 8], psm, (short)m->field_18.field_0, (short)m->field_18.field_4, 0, 0);
    psm = (m->field_0.field_8 == 0x10) ? 0xA : 0;
    zpsm = (zBufferDepth == 0x10) ? 0x3A : 0x31;
    sceGsSetDefDrawEnv(&q[0x60 / 8], psm, (short)m->field_0.field_0, (short)m->field_0.field_4, 2, zpsm);
    psm = (m->field_0.field_8 == 0x10) ? 0xA : 0;
    zpsm = (zBufferDepth == 0x10) ? 0x3A : 0x31;
    sceGsSetDefDrawEnv2(&q[0xE0 / 8], psm, (short)m->field_0.field_0, (short)m->field_0.field_4, 2, zpsm);
    psm = (m->field_0.field_8 == 0x10) ? 0xA : 0;
    zpsm = (zBufferDepth == 0x10) ? 0x3A : 0x31;
    sceGsSetDefDrawEnv(&q[0x1D0 / 8], psm, (short)m->field_0.field_0, (short)m->field_0.field_4, 2, zpsm);
    psm = (m->field_0.field_8 == 0x10) ? 0xA : 0;
    zpsm = (zBufferDepth == 0x10) ? 0x3A : 0x31;
    sceGsSetDefDrawEnv2(&q[0x250 / 8], psm, (short)m->field_0.field_0, (short)m->field_0.field_4, 2, zpsm);

    // [C 0x1FDAA0..0x1FDB30] giftag0 / giftag1: NLOOP 16, EOP, PACKED, NREG 1, A+D (zeroed first)
    q[0x50 / 8] = 0;
    q[0x58 / 8] = 0;
    q[0x1C0 / 8] = 0;
    q[0x1C8 / 8] = 0;
    q[0x1C8 / 8] = (q[0x1C8 / 8] & ~0xFULL) | 0xE;
    q[0x50 / 8] = (((q[0x50 / 8] & ~0x7FFFULL) | 0x10 | 0x8000) & 0x0FFFFFFFFFFFFFFFULL) | 0x1000000000000000ULL;
    q[0x58 / 8] = (q[0x58 / 8] & ~0xFULL) | 0xE;
    q[0x1C0 / 8] = (((q[0x1C0 / 8] & ~0x7FFFULL) | 0x10 | 0x8000) & 0x0FFFFFFFFFFFFFFFULL) | 0x1000000000000000ULL;

    // [C 0x1FDB2C..0x1FDDCC] frame buffer pages, then the Z buffer after them
    if (m->field_33 != 0) {
        t = skyStartPages(m->field_0.field_0, m->field_0.field_4, m->field_0.field_8);
        q[0xE0 / 8] = skyStartSetBase(q[0xE0 / 8], t);
        q[0x38 / 8] = skyStartSetBase(q[0x38 / 8], t);
        q[0x60 / 8] = skyStartSetBase(q[0x60 / 8], t);
        t = t << 1;
    } else {
        t = skyStartPages(m->field_18.field_0, m->field_18.field_4, m->field_18.field_8);
        if (m->field_31 == 2) {
            q[0x38 / 8] = skyStartSetBase(q[0x38 / 8], t);
            t = t << 1;
        }
        q[0x1D0 / 8] = skyStartSetBase(q[0x1D0 / 8], t);
        q[0x250 / 8] = skyStartSetBase(q[0x250 / 8], t);
        u = skyStartPages(m->field_0.field_0, m->field_0.field_4, m->field_0.field_8);
        if (m->field_30 == 2) {
            t = t + u;
        }
        q[0xE0 / 8] = skyStartSetBase(q[0xE0 / 8], t);
        q[0x60 / 8] = skyStartSetBase(q[0x60 / 8], t);
        t = t + u;
    }
    q[0x70 / 8] = skyStartSetBase(q[0x70 / 8], t);
    q[0xF0 / 8] = skyStartSetBase(q[0xF0 / 8], t);
    q[0x1E0 / 8] = skyStartSetBase(q[0x1E0 / 8], t);
    q[0x260 / 8] = skyStartSetBase(q[0x260 / 8], t);
    textureBankStart = (t + skyStartPages(m->field_0.field_0, m->field_0.field_4, zBufferDepth)) << 11;

    // [C 0x1FDDCC..0x1FDEF4] interlaced output
    f2 = m->field_18.field_C;
    if ((f2 & 2) != 0) {
        if ((f2 & 0x200) != 0) {
            unsigned long long disp0;
            unsigned long long disp1;
            unsigned int dh;

            q[0x338 / 8] = q[0x18 / 8];
            q[0x330 / 8] = q[0x10 / 8];
            q[0x348 / 8] = q[0x40 / 8];
            q[0x340 / 8] = q[0x38 / 8];
            *((unsigned char *)flip + 0x29) = 0x80;
            disp0 = q[0x18 / 8];
            disp1 = q[0x40 / 8];
            *((unsigned char *)flip + 0x01) = 0x80;
            dh = ((unsigned int)(disp0 >> 44) & 0x7FF) - 1;
            q[0x28 / 8] = q[0x28 / 8] | 1 | 2;
            q[0x10 / 8] = (q[0x10 / 8] & 0xFFC007FFFFFFFFFFULL) | 0x80000000000ULL;
            q[0x18 / 8] = (disp0 & 0xFF800FFFFFFFFFFFULL) | ((unsigned long long)(dh & 0x7FF) << 44);
            dh = ((unsigned int)(disp1 >> 44) & 0x7FF) - 1;
            q[0x00 / 8] = q[0x00 / 8] | 1 | 2;
            q[0x38 / 8] = (q[0x38 / 8] & 0xFFC007FFFFFFFFFFULL) | 0x80000000000ULL;
            q[0x40 / 8] = (disp1 & 0xFF800FFFFFFFFFFFULL) | ((unsigned long long)(dh & 0x7FF) << 44);
        }
        SyncDCache(flip, (char *)flip + 0x44F);
    }

    // 7.
    skyTexCacheOpenCache(textureBankStart, 0x100000 - textureBankStart);
    cacheOpened = 1;
    skyTexCacheRestoreRequired = 0;
    skySuspended = 0;

    // 8. [C 0x1FDF2C] the VIF1 chain at D_004B1400 (TEST_1, TEXA, ALPHA_1)
    sceDmaSend(sceDmaGetChan(1), (unsigned int)D_004B1400);
    while (sceGsSyncPath(1, 0) != 0) {
    }

    // 9. [C 0x1FDF48..0x1FE064] alpha test on (ATE, GEQUAL, AREF 0x40, FB_ONLY), draw11's offset = draw01's, the saved
    // copies of disp[0], disp[1] and the +0x330 block
    {
        unsigned long long test01 = q[0xD0 / 8] | 0x140B;
        unsigned long long test11 = q[0x240 / 8] | 0x140B;
        int k;

        q[0x1F0 / 8] = q[0x80 / 8];
        b3hw_write32(0x1000E010, 2);   // [C] D_STAT: clear channel 1's interrupt status
        q[0xD0 / 8] = test01;
        q[0x240 / 8] = test11;
        for (k = 0; k < 5; k++) {
            q[0x350 / 8 + k] = q[0x00 / 8 + k];
        }
        for (k = 0; k < 5; k++) {
            q[0x378 / 8 + k] = q[0x28 / 8 + k];
        }
        for (k = 0; k < 4; k++) {
            q[0x3A0 / 8 + k] = q[0x330 / 8 + k];
        }
    }
    SyncDCache(flip, (char *)flip + 0x44F);

    // 10.
    skyFrameBit = 1;
    sceGsSwapDBuffDc(flip, 0);
    while (sceGsSyncPath(1, 0) != 0) {
    }
    b3hw_write32(0x1000E010, 4);   // [C] D_STAT: clear channel 2's interrupt status

    // 11. / 12.
    flip->field_3CC = 0;
    flip->field_3C8 = 0;
    flip->field_3C4 = 0;
    flip->field_3C0 = 0;
    r = _rwDMADmaOpen();
    if (r == 0) {
        skyTexCacheCloseCache();
        return r;
    }

    // 13. [C 0x1FE0C8..0x1FE130] the GS register shadows game code reads
    {
        unsigned long long xy = q[0x80 / 8] & 0xFFFFFFF7FFFFFFFFULL;   // OFY's half-pixel bit (35) cleared

        skyYOff = (unsigned long long)(((long long)xy >> 36) & 0xFFF);
        skyXOff = (unsigned long long)(((long long)xy >> 4) & 0xFFF);
        skyFrame_1 = q[0x60 / 8];
        skyZbuf_1 = q[0x70 / 8];
        skyTest_1 = q[0xD0 / 8];
        skyXyoffset_1 = xy;
    }
    D_004E28B0 = 1;

    // 14. - 17.
    setInitialRenderState();
    if ((m->field_0.field_C & 4) != 0) {
        _rwDMAAsyncPktPtr[0] = 1;
    } else {
        _rwDMAAsyncPktPtr[0] = 0;
    }
    if ((m->field_18.field_C & 0x100) != 0) {
        setUpFSAA0(flip);
    }
    engineStarted = 1;
    return r;
}

// 0x001FF360
extern "C" UnkStruct_dGSkyDevice *_rwDeviceGetHandle(void)
{
    return &D_0049AD38;
}

// [C] the frame buffer rasters are destroyed only while the engine is started; returns engineStarted's value.
// 0x001FF370
extern "C" int _rpSkyDestroyFrameBufferRasters(void)
{
    int started;

    started = engineStarted;
    if (started != 0) {
        if (skyDrawRaster != 0) {
            RwRasterDestroy(skyDrawRaster);
            skyDrawRaster = 0;
        }
        if (skyDisplayRaster != 0) {
            RwRasterDestroy(skyDisplayRaster);
            skyDisplayRaster = 0;
        }
    }
    return started;
}

// [C] the pipelines are overloaded first and taken back when the raster start or the frame buffer rasters fail;
// on success the driver's matrix and vector multiply functions are installed. skyProj becomes the identity (flags |=
// 0x20003) and skyProjState 0xDEADDEAD either way. Returns 1 when both succeeded, else 0.
// 0x001FC928
extern "C" int SkyFinalizeStart(void)
{
    int result = 0;

    _rwSkyOverloadPipelines();
    if (_rwSkyRasterFinaliseStart() != 0) {
        result = _rpSkyCreateFrameBufferRasters() != 0;
    }
    if (result == 0) {
        _rwSkyUnOverloadPipelines();
    } else {
        _rwMatrixSetMultFn(func_001FFE18);
        _rwVectorSetMultFn((UnkFn_001DE550_3)func_001FFE90, func_001FFEF0, (UnkFn_001DE550_3)func_001FFF80, func_001FFFD8);
    }
    skyProj.field_30 = 0.0f;
    skyProj.field_0 = 1.0f;
    skyProj.field_C = skyProj.field_C | 0x20003;
    skyProj.field_28 = 1.0f;
    skyProj.field_14 = 1.0f;
    skyProj.field_10 = 0.0f;
    skyProj.field_8 = 0.0f;
    skyProj.field_4 = 0.0f;
    skyProj.field_24 = 0.0f;
    skyProj.field_20 = 0.0f;
    skyProj.field_18 = 0.0f;
    skyProj.field_38 = 0.0f;
    skyProj.field_34 = 0.0f;
    skyProjState = 0xDEADDEAD;
    return result;
}

// [C] a 0x17-entry jump table (jtbl_004B8EA0); request >= 0x17 and 0xD..0x10, 0x14, 0x15 return 0 ([L] 3.1 request
// names: 0 OPEN, 1 CLOSE, 2 START, 3 STOP, 4 REGISTER, 5 GETNUMMODES, 6 GETMODEINFO, 7 USEMODE, 8 FOCUS,
// 9 INITPIPELINE, 0xA GETMODE, 0xB STANDARDS, 0xC GETTEXMEMSIZE, 0x11 FINALIZESTART, 0x12 INITIATESTOP,
// 0x13 GETMAXTEXTURESIZE, 0x16 GETID).
// 0x001FE1A0
extern "C" int SkySystem(int request, void *out, void *inOut, int numIn)
{
    int result;
    int depth;
    int i;
    UnkStruct_SkyStandards *std;

    switch (request) {
    case 0:
        result = SkyOpen();
        break;
    case 1:
    case 8:
    case 9:
        result = 1;
        break;
    case 2:
        result = SkyStart();
        break;
    case 3:
        // [C] retail stores 0x3C0, 0x3CC, 0x3C8, then 0x3C4 in the delay slot of the skyTexCacheCloseCache call
        _rwDMADmaClose();
        _rwDMAFlipData.field_3C0 = 0;
        _rwDMAFlipData.field_3CC = 0;
        _rwDMAFlipData.field_3C8 = 0;
        _rwDMAFlipData.field_3C4 = 0;
        skyTexCacheCloseCache();
        result = 1;
        engineStarted = 0;
        needCache = 1;
        cacheOpened = 0;
        break;
    case 4:
        // [C] the 0x38-byte device is copied with unaligned ldl/ldr + sdl/sdr pairs
        *(UnkStruct_dGSkyDevice *)out = *_rwDeviceGetHandle();
        rwDMASavedGP = inOut;
        result = 1;
        break;
    case 5:
        *(int *)out = 0x59;
        result = 1;
        break;
    case 6:
        // [C] modes -1 (customMode's first 0x18 bytes) .. 0x58
        result = (unsigned int)(numIn + 1) < 0x5A;
        if (result == 0) {
            break;
        }
        if (numIn < 0) {
            *(UnkStruct_videoModes *)out = customMode.field_0;
        } else {
            *(UnkStruct_videoModes *)out = videoModes[numIn];
        }
        break;
    case 7:
        // [C] refused while started; currentMode is stored before the format is chosen
        result = 0;
        if (engineStarted == 0 && numIn < 0x59) {
            result = numIn >= -1;
        }
        if (result == 0) {
            break;
        }
        currentMode = numIn;
        if (numIn < 0) {
            depth = customMode.field_0.field_8;
        } else {
            depth = videoModes[numIn].field_8;
        }
        if (depth == 0x20) {
            currentCameraFormat = 0x500;
        } else {
            currentCameraFormat = 0x100;
        }
        break;
    case 0xA:
        *(int *)out = currentMode;
        result = 1;
        break;
    case 0xB:
        // [C] every slot gets func_001FFE10, then saGStandards (0x0049AC58, 0x1C {index, function} pairs, written by
        // no code [C xref]) is applied in its order to the slots below numIn; spelled out here so the guest addresses
        // are stored without reading the table.
        std = (UnkStruct_SkyStandards *)out;
        for (i = 0; i < numIn; i++) {
            std[i].field_0 = func_001FFE10;
        }
        if (0x1 < numIn) std[0x1].field_0 = (int (*)(void *, void *, int))SkyCameraBeginUpdate;
        if (0xA < numIn) std[0xA].field_0 = (int (*)(void *, void *, int))SkyCameraEndUpdate;
        if (0x15 < numIn) std[0x15].field_0 = SkyCameraClear;
        if (0x14 < numIn) std[0x14].field_0 = SkyRasterShowRaster;
        if (0x2 < numIn) std[0x2].field_0 = func_00207E30;
        if (0x3 < numIn) std[0x3].field_0 = func_00207F28;
        if (0x7 < numIn) std[0x7].field_0 = _rwSkyRasterSetImage;
        if (0x6 < numIn) std[0x6].field_0 = _rwSkyImageGetRaster;
        if (0x5 < numIn) std[0x5].field_0 = (int (*)(void *, void *, int))SkyRasterDestroy;
        if (0x4 < numIn) std[0x4].field_0 = (int (*)(void *, void *, int))SkyRasterCreate;
        if (0xC < numIn) std[0xC].field_0 = func_001F9DC0;
        if (0x9 < numIn) std[0x9].field_0 = _rwSkyImageFindRasterFormat;
        if (0x8 < numIn) std[0x8].field_0 = func_001FFB50;
        if (0xF < numIn) std[0xF].field_0 = SkyRasterLock;
        if (0x10 < numIn) std[0x10].field_0 = SkyRasterUnlock;
        if (0x17 < numIn) std[0x17].field_0 = SkyRasterLockPalette;
        if (0x18 < numIn) std[0x18].field_0 = SkyRasterUnlockPalette;
        if (0xE < numIn) std[0xE].field_0 = _rwSkyRasterClear;
        if (0xD < numIn) std[0xD].field_0 = _rwSkyRasterClearRect;
        if (0x11 < numIn) std[0x11].field_0 = _rwSkyRasterRender;
        if (0x12 < numIn) std[0x12].field_0 = _rwSkyRasterRenderScaled;
        if (0x13 < numIn) std[0x13].field_0 = _rwSkyRasterRenderFast;
        if (0xB < numIn) std[0xB].field_0 = func_00205E30;
        if (0x16 < numIn) std[0x16].field_0 = func_001FFE08;
        if (0x19 < numIn) std[0x19].field_0 = SkyNativeTextureGetSize;
        if (0x1B < numIn) std[0x1B].field_0 = SkyNativeTextureWrite;
        if (0x1A < numIn) std[0x1A].field_0 = SkyNativeTextureRead;
        if (0x1C < numIn) std[0x1C].field_0 = func_001FFD18;
        result = 1;
        break;
    case 0xC:
        // [C] the result is engineStarted's value itself, not a 0/1
        result = engineStarted;
        if (result != 0) {
            *(unsigned int *)out = (0x100000 - textureBankStart) << 2;
        }
        break;
    case 0x11:
        result = SkyFinalizeStart();
        break;
    case 0x12:
        _rwSkyRasterInitiateStop();
        _rpSkyDestroyFrameBufferRasters();
        _rwSkyUnOverloadPipelines();
        result = 1;
        break;
    case 0x13:
        *(int *)out = 0x400;
        result = 1;
        break;
    case 0x16:
        *(short *)out = 6;
        result = 1;
        break;
    default:
        result = 0;
        break;
    }
    return result;
}
