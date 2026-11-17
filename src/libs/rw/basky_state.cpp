#include "rw/skyglobals.h"
// RenderWare 3.6 sky2 driver: the render state set / get functions and the FSAA shrink-blit packets (RW 3.1 file name:
// driver/sky2/basky.c; reading aid only). Written from the retail asm (decomp assembly/asm/rw/renderware.s). Tags: [C]
// confirmed from the asm, [L], [G]. boot2. Kept apart from basky.cpp (another lane edits it); the structs below are
// this file's own views of the same objects (same layouts).
//
// The GS register shadows (skyTest_1 ...) are 64-bit GS register values; each state change that reaches the GS builds
// a small packet: _rwDMAOpenVIFPkt(0x80000000, nQW) reserves it and leaves the cursor farFogPlane at the first slot,
// then a GIFtag {NLOOP = nQW - 1, EOP, PACKED, NREG 1; REGS = 0xE (A+D)} and one (data, register) qword per register,
// each stored with sq and followed by the cursor advance, as the asm does.

// [C] one 128-bit slot of a packet: the sq of a pcpyld pair stores the low doubleword at +0, the high one at +8.
struct UnkStruct_farFogPlane {
    unsigned long long field_0;   // [C] GIFtag low / A+D data
    unsigned long long field_8;   // [C] GIFtag REGS / A+D register number
};

// [C] customMode: 0x34 bytes (symbol table); the fields setUpFSAA0 reads ([L] RpSkyVideoMode: the RwVideoMode at +0,
// then the display size, depth and flags; basky.cpp: what SkyStart completes).
struct UnkStruct_customMode {
    int field_0;                    // [C] the FRAME / CLAMP / TEX0 width ([L] draw width)
    int field_4;                    // [C] the CLAMP height ([L] draw height)
    unsigned char pad_8[0x10];
    int field_18;                   // [C] the ratio divisor for the width, the XYZ2 x ([L] display width)
    int field_1C;                   // [C] the ratio divisor for the height, the XYZ2 y ([L] display height)
    int field_20;                   // [C] DTHE = (unsigned)field_20 < 0x18 ([L] depth)
    unsigned int field_24;          // [C] bit 1 selects t4 = 0 ([L] flags; 2 = interlace [L sky2_handlers.md §6])
    unsigned char pad_28[0xC];
};

// [C] _rwDMAFlipData: 0x3D0 bytes (symbol table); the doublewords setUpFSAA0 reads (GS register values: +0x10 / +0x360
// DISPFB, +0x60 / +0x1D0 FRAME, +0x70 ZBUF, +0x1E0 ZBUF [L sky2_handlers.md §6]) and the four packet slots it fills.
struct UnkStruct_rwDMAFlipData {
    unsigned char pad_0[0x10];
    unsigned long long field_10;    // [C] FRAME_1 source when t4 == 0 (PSM bits 15..19, base bits 0..8)
    unsigned char pad_18[0x48];
    unsigned long long field_60;    // [C] TEX0 source for even i; copied to the last FRAME_1
    unsigned char pad_68[0x8];
    unsigned long long field_70;    // [C] copied to the last ZBUF_1
    unsigned char pad_78[0x158];
    unsigned long long field_1D0;   // [C] TEX0 source for odd i
    unsigned char pad_1D8[0x8];
    unsigned long long field_1E0;   // [C] the first ZBUF_1's source (bits 24..27 PSM, 0..8 base)
    unsigned char pad_1E8[0x178];
    unsigned long long field_360;   // [C] FRAME_1 source when t4 != 0
    unsigned char pad_368[0x58];
    UnkStruct_farFogPlane *field_3C0;   // [C] &dmaData1 (read by rwDMAVSYNCHandler [L sky2_handlers.md §6])
    UnkStruct_farFogPlane *field_3C4;   // [C] &dmaData2
    UnkStruct_farFogPlane *field_3C8;   // [C] &dmaData3
    UnkStruct_farFogPlane *field_3CC;   // [C] &dmaData4
};

extern unsigned long long skyZbuf_1;            // [C] 0x004E1830
extern unsigned long long skyTest_1;            // [C] 0x004E1840
extern unsigned long long skyXyoffset_1;        // [C] 0x004E1848
extern unsigned long long skyFogcol;            // [C] 0x004E1850
extern unsigned long long skyClamp_1;           // [C] 0x004E1858
extern unsigned long long skyTex1_1;            // [C] 0x004E1860
extern unsigned long long skyAlpha_1;           // [C] 0x004E1868
extern unsigned long long skyPrim_State;        // [C] 0x004E1878
extern int skySrcBlend;                         // [C] 0x004E1880
extern int skyDestBlend;                        // [C] 0x004E1884
extern unsigned char skyUploadedCode;           // [C] 0x004E18D0 (a u32 symbol; the asm uses its first byte: lbu / sb)
extern int skyAlphaTex;                         // [C] 0x004E2864
extern int skyVertexAlpha;                      // [C] 0x004E2868
extern int gSkyCullState;                       // [C] 0x004E286C
extern int skyBlendStates[];                    // [C] 0x0049ABC8, 0x90 bytes: [dest * 6 + src] ALPHA_1 values (lw)
extern unsigned char skyLinearFog[];            // [C] 0x00675F90, 0xE0 bytes
extern unsigned char skyExpFog[];               // [C] 0x00676070, 0xE0 bytes
extern unsigned char skyExp2Fog[];              // [C] 0x00676150, 0xE0 bytes
extern float D_004E0710;                        // [C] 0x004E0710 (lit4): 0x3F008088 (0.50196123), the alpha ref scale
extern float D_004E0714;                        // [C] 0x004E0714 (lit4): 0x3FFF0007 (1.9921883), its inverse
extern UnkStruct_customMode customMode;         // [C] 0x00675F10
// [C] the four FSAA packets, 0x140 bytes each; their table names dmaData1.295 .. dmaData4.298 are not C identifiers.
extern UnkStruct_farFogPlane D_00675900[];      // [C] dmaData1.295
extern UnkStruct_farFogPlane D_00675A40[];      // [C] dmaData2.296
extern UnkStruct_farFogPlane D_00675B80[];      // [C] dmaData3.297
extern UnkStruct_farFogPlane D_00675CC0[];      // [C] dmaData4.298

// [C] a0 = 0x80000000 (lui: the register holds 0xFFFFFFFF80000000), a1 = nQW; its result is never read here.
extern "C" int _rwDMAOpenVIFPkt(unsigned int type, int nQW);
extern "C" void RpSkyTexCacheAccessSpeculate(void *raster);          // [C] result not read
extern "C" int RpSkyTexCacheAccessRaster(void *raster, int flag);   // [C] called with flag 0; result tested
// [C] compiler runtime, called by name as the asm does: fptoui (float in $f12 -> unsigned in v0), __floatdisf (a 64-bit
// integer in a0 -> float in $f0)
extern "C" unsigned int fptoui(float f);
extern "C" float __floatdisf(long long i);
extern "C" void SyncDCache(void *start, void *end);

// [C] one packet qword at the cursor (sq: low doubleword first), then the cursor advance (sw farFogPlane), the
// sequence every case of _rwSkySetRenderState repeats.
static inline void func_001F4668_inl1(unsigned long long lo, unsigned long long hi)
{
    UnkStruct_farFogPlane *p;

    p = (UnkStruct_farFogPlane *)farFogPlane;
    p->field_0 = lo;
    p->field_8 = hi;
    farFogPlane = (RwUInt32)(p + 1);
}

// [C] the 2-qword packet: GIFtag 0x1000000000008001 / 0xE (NLOOP 1), then (value, reg).
static inline void func_001F4668_inl2(unsigned long long value, unsigned long long reg)
{
    _rwDMAOpenVIFPkt(0x80000000, 2);
    func_001F4668_inl1(0x1000000000008001ULL, 0xE);
    func_001F4668_inl1(value, reg);
}

// [C] a 0x1E-entry jump table (jtbl_004B8420) over state - 1; states >= 0x1F, 0 and 0xD, 0x11..0x13, 0x15..0x1C return 0
// ([L] RW 3.1 RwRenderState: 1 TEXTURERASTER, 2 TEXTUREADDRESS, 3 ADDRESSU, 4 ADDRESSV, 5 TEXTUREPERSPECTIVE,
// 6 ZTESTENABLE, 7 SHADEMODE, 8 ZWRITEENABLE, 9 TEXTUREFILTER, 0xA SRCBLEND, 0xB DESTBLEND, 0xC VERTEXALPHAENABLE,
// 0xE FOGENABLE, 0xF FOGCOLOR, 0x10 FOGTYPE, 0x14 CULLMODE, 0x1D ALPHATESTFUNCTION, 0x1E ALPHATESTFUNCTIONREF).
// The value is the 32-bit a1 (the asm compares it with sltiu / sltu: unsigned).
// 0x001F4668
extern "C" int _rwSkySetRenderState(int state, unsigned int value)
{
    int result;
    unsigned long long test;
    unsigned long long prim;
    int blend;
    int n;
    float f;

    switch (state) {
    case 1:
        // [C] the raster is stored first; s1 (the result) starts at 1 and becomes 0 only when the cache refuses it
        result = 1;
        skyTextureRaster = value;
        if (value == 0) {
            prim = skyPrim_State;
            skyAlphaTex = 0;
            skyPrim_State = prim & ~0x10ULL;
        } else {
            RpSkyTexCacheAccessSpeculate((void *)value);
            if (RpSkyTexCacheAccessRaster((void *)skyTextureRaster, 0) != 0) {
                skyPrim_State = skyPrim_State | 0x10;
                // [C] byte +0x23 of the raster, low nibble ([L] the raster format: 6 = no alpha)
                if ((((unsigned char *)(void *)skyTextureRaster)[0x23] & 0xF) != 6) {
                    skyAlphaTex = 1;
                } else {
                    skyAlphaTex = 0;
                }
            } else {
                prim = skyPrim_State;
                skyAlphaTex = 0;
                result = 0;
                skyTextureRaster = 0;
                skyPrim_State = prim & ~0x10ULL;
            }
        }
        if (skyVertexAlpha != 0 || skyAlphaTex != 0) {
            skyPrim_State = skyPrim_State | 0x40;
        } else {
            skyPrim_State = skyPrim_State & ~0x40ULL;
        }
        // [C] TEST_1.ATE (bit 0) follows skyAlphaTex; a packet only when it changes
        test = skyTest_1;
        if (skyAlphaTex != 0) {
            if ((test & 1) != 0) {
                return result;
            }
            skyTest_1 = test | 1;
        } else {
            if ((test & 1) == 0) {
                return result;
            }
            skyTest_1 = test & ~1ULL;
        }
        func_001F4668_inl2(skyTest_1, 0x47);
        return result;
    case 2:
        // [C] 1 -> 0 (REPEAT both), 3 -> 5 (CLAMP both), anything else 0
        if (value == 1) {
            skyClamp_1 = 0;
        } else if (value == 3) {
            skyClamp_1 = 5;
        } else {
            return 0;
        }
        func_001F4668_inl2(skyClamp_1, 0x08);
        return 1;
    case 3:
        // [C] WMS (bits 0..1): 1 clears them, 3 sets bit 0
        if (value == 1) {
            skyClamp_1 = skyClamp_1 & ~3ULL;
        } else if (value == 3) {
            skyClamp_1 = skyClamp_1 | 1;
        } else {
            return 0;
        }
        func_001F4668_inl2(skyClamp_1, 0x08);
        return 1;
    case 4:
        // [C] WMT (bits 2..3): 1 clears them, 3 sets bit 2
        if (value == 1) {
            skyClamp_1 = skyClamp_1 & ~0xCULL;
        } else if (value == 3) {
            skyClamp_1 = skyClamp_1 | 4;
        } else {
            return 0;
        }
        func_001F4668_inl2(skyClamp_1, 0x08);
        return 1;
    case 5:
        // [C] FALSE is refused; anything else clears bit 8 of the PRIM shadow
        if (value == 0) {
            return 0;
        }
        skyPrim_State = skyPrim_State & ~0x100ULL;
        return 1;
    case 6:
        // [C] ZTE / ZTST (bits 16..18): 0x50000 (GEQUAL) when enabled, 0x30000 (ALWAYS) when not
        if (value != 0) {
            skyTest_1 = (skyTest_1 & 0xFFFFFFFFFFF8FFFFULL) | 0x50000;
        } else {
            skyTest_1 = (skyTest_1 & 0xFFFFFFFFFFF8FFFFULL) | 0x30000;
        }
        func_001F4668_inl2(skyTest_1, 0x47);
        return 1;
    case 7:
        // [C] 1 clears PRIM.IIP (bit 3), 2 sets it
        if (value == 1) {
            skyPrim_State = skyPrim_State & ~8ULL;
        } else if (value == 2) {
            skyPrim_State = skyPrim_State | 8;
        } else {
            return 0;
        }
        return 1;
    case 8:
        // [C] ZBUF.ZMSK (bit 32); enabling also sets ZTST ALWAYS when TEST_1 bit 16 (ZTE) is clear
        if (value != 0) {
            test = skyTest_1;
            skyZbuf_1 = skyZbuf_1 & 0xFFFFFFFEFFFFFFFFULL;
            if ((test & 0x10000) == 0) {
                skyTest_1 = (test & 0xFFFFFFFFFFF8FFFFULL) | 0x30000;
            }
        } else {
            skyZbuf_1 = skyZbuf_1 | 0x100000000ULL;
        }
        _rwDMAOpenVIFPkt(0x80000000, 3);
        func_001F4668_inl1(0x1000000000008002ULL, 0xE);
        func_001F4668_inl1(skyZbuf_1, 0x4E);
        func_001F4668_inl1(skyTest_1, 0x47);
        return 1;
    case 9:
        // [C] jtbl_004B84A0 over value - 1: TEX1 MMAG / MMIN (bits 5..8)
        switch (value) {
        case 1: skyTex1_1 = skyTex1_1 & ~0x1E0ULL; break;
        case 2: skyTex1_1 = (skyTex1_1 & ~0x1E0ULL) | 0x60; break;
        case 3: skyTex1_1 = (skyTex1_1 & ~0x1E0ULL) | 0x80; break;
        case 4: skyTex1_1 = (skyTex1_1 & ~0x1E0ULL) | 0x120; break;
        case 5: skyTex1_1 = (skyTex1_1 & ~0x1E0ULL) | 0xC0; break;
        case 6: skyTex1_1 = (skyTex1_1 & ~0x1E0ULL) | 0x160; break;
        default: return 0;
        }
        func_001F4668_inl2(skyTex1_1, 0x14);
        return 1;
    case 0xA:
    case 0xB:
        // [C] jtbl_004B84C0 (source) / jtbl_004B84E0 (destination) over value - 1: 1..8 -> 0, 1, refused, refused,
        // 2, 3, 4, 5
        switch (value) {
        case 1: blend = 0; break;
        case 2: blend = 1; break;
        case 5: blend = 2; break;
        case 6: blend = 3; break;
        case 7: blend = 4; break;
        case 8: blend = 5; break;
        default: return 0;
        }
        if (state == 0xA) {
            skySrcBlend = blend;
        } else {
            skyDestBlend = blend;
        }
        // [C] ALPHA_1 from the table (sign-extended word); a negative entry is refused after the store; src == dest ==
        // 1 also sets bit 39 (FIX = 0x80)
        n = skyBlendStates[skyDestBlend * 6 + skySrcBlend];
        skyAlpha_1 = (unsigned long long)(long long)n;
        if (n < 0) {
            return 0;
        }
        if (skySrcBlend == 1 && skyDestBlend == skySrcBlend) {
            skyAlpha_1 = (unsigned long long)(long long)n | 0x8000000000ULL;
        }
        func_001F4668_inl2(skyAlpha_1, 0x42);
        return 1;
    case 0xC:
        // [C] PRIM.ABE (bit 6) = vertex alpha || texture alpha
        skyVertexAlpha = value != 0;
        if (value != 0 || skyAlphaTex != 0) {
            skyPrim_State = skyPrim_State | 0x40;
        } else {
            skyPrim_State = skyPrim_State & ~0x40ULL;
        }
        return 1;
    case 0xE:
        // [C] PRIM.FGE (bit 5) and bit 0 of skyUploadedCode's first byte, PRIM stored first
        if (value != 0) {
            skyPrim_State = skyPrim_State | 0x20;
            skyUploadedCode = skyUploadedCode | 1;
        } else {
            skyPrim_State = skyPrim_State & ~0x20ULL;
            skyUploadedCode = skyUploadedCode & 0xFE;
        }
        return 1;
    case 0xF: {
        // [C] the packet is opened before the shadow is written; RGBA -> FOGCOL (R and B swapped)
        unsigned long long fog;

        _rwDMAOpenVIFPkt(0x80000000, 2);
        fog = ((value >> 16) & 0xFF) | (value & 0xFF00) | ((value & 0xFF) << 16);
        skyFogcol = fog;
        func_001F4668_inl1(0x1000000000008001ULL, 0xE);
        func_001F4668_inl1(fog, 0x3D);
        return 1;
    }
    case 0x10:
        // [C] only 1 (linear) is accepted
        if (value != 1) {
            return 0;
        }
        fogTable = (void *)skyLinearFog;
        return 1;
    case 0x14:
        // [C] bit 5 of skyUploadedCode's first byte: cleared for 1, set otherwise; the mode is stored as given
        if (value == 1) {
            skyUploadedCode = skyUploadedCode & 0xDF;
        } else {
            skyUploadedCode = skyUploadedCode | 0x20;
        }
        gSkyCullState = value;
        return 1;
    case 0x1D:
        // [C] jtbl_004B8500 over value - 1: ATST (bits 0..3, ATE included) = 1, 5, 9, 7, 0xD, 0xF, 0xB, 2; when ATE
        // (bit 0) ends up set, AFAIL (bits 12..13) is cleared
        switch (value) {
        case 1: n = 0x1; break;
        case 2: n = 0x5; break;
        case 3: n = 0x9; break;
        case 4: n = 0x7; break;
        case 5: n = 0xD; break;
        case 6: n = 0xF; break;
        case 7: n = 0xB; break;
        case 8: n = 0x2; break;
        default: return 0;
        }
        skyTest_1 = (skyTest_1 & ~0xFULL) | (unsigned long long)n;
        test = skyTest_1;
        if ((test & 1) != 0) {
            skyTest_1 = test & ~0x3000ULL;
        }
        func_001F4668_inl2(skyTest_1, 0x47);
        return 1;
    case 0x1E:
        // [C] AREF (bits 4..11) = (int)((float)value * D_004E0710) << 4 (32-bit sll, sign-extended, ORed in whole);
        // stored before the packet is opened
        f = (float)(int)value * D_004E0710;
        n = (int)f;
        skyTest_1 = (unsigned long long)(long long)(int)((unsigned int)n << 4) | (skyTest_1 & ~0xFF0ULL);
        func_001F4668_inl2(skyTest_1, 0x47);
        return 1;
    default:
        return 0;
    }
}

// [C] a 0x1E-entry jump table (jtbl_004B8520) over state - 1, the inverse of _rwSkySetRenderState: a 32-bit store at
// out and 1, or 0 (states it does not know, and the values it cannot express: no store then, except where noted).
// 0x001F5100
extern "C" int SkyGetRenderState(int state, void *out)
{
    unsigned long long c;
    unsigned long long a;
    int src;
    int dest;
    int idx;
    int n;
    unsigned int v;
    float f;

    switch (state) {
    case 1:
        // [C] nor + movz: -1 reads as 0
        n = (int)skyTextureRaster;
        if (n == -1) {
            n = 0;
        }
        *(int *)out = n;
        return 1;
    case 2:
        // [C] WMS and WMT must agree (bits 0..1 against 2..3: only when both are 0), else 0 is stored
        c = skyClamp_1;
        if ((c & 3) != (c & 0xC)) {
            *(int *)out = 0;
        } else if ((c & 3) != 0) {
            *(int *)out = 3;
        } else {
            *(int *)out = 1;
        }
        return 1;
    case 3:
        *(int *)out = (skyClamp_1 & 3) != 0 ? 3 : 1;
        return 1;
    case 4:
        *(int *)out = (skyClamp_1 & 0xC) != 0 ? 3 : 1;
        return 1;
    case 5:
        *(int *)out = (skyPrim_State & 0x100) == 0 ? 1 : 0;
        return 1;
    case 6:
        // [C] ZTST bits 17..18 == 2 (GEQUAL)
        *(int *)out = ((skyTest_1 & 0x60000) ^ 0x40000) == 0;
        return 1;
    case 7:
        *(int *)out = (skyPrim_State & 8) != 0 ? 2 : 1;
        return 1;
    case 8:
        // [C] lwu of the ZBUF high word: !ZMSK
        *(int *)out = ((unsigned int)(skyZbuf_1 >> 32) ^ 1) & 1;
        return 1;
    case 9:
        // [C] TEX1 bits 5..8: 0 -> 1, 0x60 -> 2, 0x80 -> 3, 0x120 -> 4, 0xC0 -> 5, 0x160 -> 6, else 0 without a store
        switch ((unsigned int)skyTex1_1 & 0x1E0) {
        case 0x0: *(int *)out = 1; break;
        case 0x60: *(int *)out = 2; break;
        case 0x80: *(int *)out = 3; break;
        case 0x120: *(int *)out = 4; break;
        case 0xC0: *(int *)out = 5; break;
        case 0x160: *(int *)out = 6; break;
        default: return 0;
        }
        return 1;
    case 0xA:
    case 0xB:
        // [C] ALPHA_1 must be non-negative and equal the table entry for (src, dest) (with bit 39 for src == dest == 1);
        // then jtbl_004B85A0 (source) / jtbl_004B85C0 (destination): 0..5 -> 1, 2, 5, 6, 7, 8; >= 6 gives 0
        a = skyAlpha_1;
        if ((long long)a < 0) {
            return 0;
        }
        dest = skyDestBlend;
        src = skySrcBlend;
        n = skyBlendStates[dest * 6 + src];
        if (src == 1 && dest == src) {
            if (a != ((unsigned long long)(long long)n | 0x8000000000ULL)) {
                return 0;
            }
        } else if (a != (unsigned long long)(long long)n) {
            return 0;
        }
        idx = state == 0xA ? src : dest;
        switch ((unsigned int)idx) {
        case 0: *(int *)out = 1; break;
        case 1: *(int *)out = 2; break;
        case 2: *(int *)out = 5; break;
        case 3: *(int *)out = 6; break;
        case 4: *(int *)out = 7; break;
        case 5: *(int *)out = 8; break;
        default: return 0;
        }
        return 1;
    case 0xC:
        *(int *)out = skyVertexAlpha;
        return 1;
    case 0xE:
        *(int *)out = (int)(skyPrim_State >> 5) & 1;
        return 1;
    case 0xF:
        // [C] FOGCOL -> RGBA with alpha 0xFF (R and B swapped back)
        c = skyFogcol;
        *(int *)out = (int)(((c & 0xFF) << 16) | (c & 0xFF00) | ((c >> 16) & 0xFF) | 0xFF000000ULL);
        return 1;
    case 0x10:
        // [C] 1 linear, 2 exponential, 3 exponential squared, else 0 (stored)
        if ((unsigned char *)fogTable == skyLinearFog) {
            *(int *)out = 1;
        } else if ((unsigned char *)fogTable == skyExpFog) {
            *(int *)out = 2;
        } else if ((unsigned char *)fogTable == skyExp2Fog) {
            *(int *)out = 3;
        } else {
            *(int *)out = 0;
        }
        return 1;
    case 0x14:
        *(int *)out = gSkyCullState;
        return 1;
    case 0x1D:
        // [C] ATE clear -> 8; else jtbl_004B85E0 over TEST & 0x300E (ATST and AFAIL): 0 -> 1, 2 -> 8, 4 -> 2, 6 -> 4,
        // 8 -> 3, 0xA -> 7, 0xC -> 5, 0xD -> 6, the other entries and >= 0xE -> 0 (stored)
        c = skyTest_1;
        if ((c & 1) == 0) {
            *(int *)out = 8;
            return 1;
        }
        switch ((unsigned int)c & 0x300E) {
        case 0x0: *(int *)out = 1; break;
        case 0x2: *(int *)out = 8; break;
        case 0x4: *(int *)out = 2; break;
        case 0x6: *(int *)out = 4; break;
        case 0x8: *(int *)out = 3; break;
        case 0xA: *(int *)out = 7; break;
        case 0xC: *(int *)out = 5; break;
        case 0xD: *(int *)out = 6; break;
        default: *(int *)out = 0; break;
        }
        return 1;
    case 0x1E:
        // [C] AREF through __floatdisf, times D_004E0714, cvt.w.s; swc1 stores the integer's bits
        f = __floatdisf((long long)((skyTest_1 & 0xFF0) >> 4));
        f = f * D_004E0714;
        v = (unsigned int)(int)f;
        *(unsigned int *)out = v;
        return 1;
    default:
        return 0;
    }
}

// [C] (float)u as MWCC converts an unsigned int: cvt.s.w when it is non-negative as an int, else the halved value
// (low bit kept) converted and doubled with add.s [C 0x1FCF70-0x1FCF9C, 0x1FCFC4-0x1FD004].
static inline float func_001FCA18_inl1(unsigned int u)
{
    float f;

    if ((int)u >= 0) {
        f = (float)(int)u;
    } else {
        f = (float)(int)((u >> 1) | (u & 1));
        f = f + f;
    }
    return f;
}

// [C] mfc1: a float's bits
static inline unsigned int func_001FCA18_inl2(float x)
{
    union {
        float f;
        unsigned int u;
    } v;

    v.f = x;
    return v.u;
}

// [C] four 0x14-qword GS packets (DMA end tag, GIFtag NLOOP 18 A+D, 18 registers), one per dmaData buffer, a sprite
// that copies the frame buffer to the display buffer at the display/draw ratio ([L] the FSAA shrink blit); then their
// addresses into flip +0x3C0..+0x3CC. The ratios A, B, C are fixed-point (x8, x8, x16) through fptoui; t4 is 0 when
// customMode.field_24 bit 1 is set, else bit 1 of i (B3: always 0 [L sky2_handlers.md §6]). Retail keeps i and the
// ratios on the stack (sp+0x0 .. +0x50); that is the only stack data.
// 0x001FCA18
extern "C" void setUpFSAA0(UnkStruct_rwDMAFlipData *flip)
{
    unsigned int i;
    unsigned int ratioA;
    unsigned int ratioB;
    unsigned int ratioC;
    int t4;
    UnkStruct_farFogPlane *pkt;
    unsigned long long dfb;
    unsigned int dfbLo;
    unsigned long long z;
    unsigned long long x;
    unsigned long long clampHW;
    unsigned int s;
    unsigned int t;
    unsigned int u;

    i = 0;
    ratioA = fptoui((float)customMode.field_0 * 8.0f / (float)customMode.field_18 + 0.5f);
    ratioB = fptoui((float)customMode.field_4 * 8.0f / (float)customMode.field_1C + 0.5f);
    ratioC = fptoui((float)customMode.field_4 * 16.0f / (float)customMode.field_1C + 0.5f);
    do {
        if (i == 0) {
            pkt = D_00675900;
        } else if (i == 1) {
            pkt = D_00675A40;
        } else if (i == 2) {
            pkt = D_00675B80;
        } else {
            pkt = D_00675CC0;
        }
        t4 = 0;
        if ((customMode.field_24 & 2) == 0) {
            t4 = ((int)i >> 1) & 1;
        }
        // +0x00 DMAtag end, QWC 0x13; +0x10 GIFtag NLOOP 18, EOP, PACKED, NREG 1, A+D
        pkt[0].field_0 = 0x70000013;
        pkt[0].field_8 = 0;
        pkt[1].field_0 = 0x1000000000008012ULL;
        pkt[1].field_8 = 0xE;
        // +0x20 FRAME_1: PSM from DISPFB bits 15..19 (dsll 17 / dsra32 then & 0x1F), FBW = display width / 64, FBP from
        // DISPFB bits 0..8; 32-bit ors, sign-extended
        if (t4 != 0) {
            dfb = flip->field_360;
            dfbLo = (unsigned int)flip->field_360;
        } else {
            dfb = flip->field_10;
            dfbLo = (unsigned int)flip->field_10;
        }
        pkt[2].field_0 = (unsigned long long)(long long)(int)(
            (((unsigned int)(dfb >> 15) & 0x1F) << 24) | ((unsigned int)(customMode.field_18 >> 6) << 16) |
            (dfbLo & 0x1FF));
        pkt[2].field_8 = 0x4C;
        // +0x30 XYOFFSET_1 = 0; +0x40 SCISSOR_1 = (0, 0x7FFF, 0, 0x7FFF); +0x50 TEST_1 = 0x30000 (ZTST ALWAYS)
        pkt[3].field_0 = 0;
        pkt[3].field_8 = 0x18;
        pkt[4].field_0 = 0x7FFF00007FFF0000ULL;
        pkt[4].field_8 = 0x40;
        pkt[5].field_0 = 0x30000;
        pkt[5].field_8 = 0x47;
        // +0x60 ZBUF_1 = PSM (field_1E0 bits 24..27), ZBP (bits 0..8), ZMSK
        z = flip->field_1E0;
        clampHW = ((unsigned long long)((long long)customMode.field_4 - 1) << 34) |
                  ((unsigned long long)((long long)customMode.field_0 - 1) << 14);
        pkt[6].field_0 = (((z >> 24) & 0xF) << 24) | ((z & 0x1FF) | 0x100000000ULL);
        pkt[6].field_8 = 0x4E;
        // +0x70 DTHE = depth < 24 (sltiu); +0x80 CLAMP_1 = REGION_CLAMP both, MAXU = width - 1, MAXV = height - 1
        pkt[7].field_0 = (unsigned int)customMode.field_20 < 0x18;
        pkt[7].field_8 = 0x45;
        pkt[8].field_0 = clampHW | 0xA;
        pkt[8].field_8 = 0x08;
        // +0x90 TEX0_1 = TW = TH = 10, TFX 1 (0xAA8000000), PSM (bits 24..29 of the FRAME value), TBW = width / 64
        // (srl, 32-bit, zero-extended), TBP0 = FBP * 32
        if ((i & 1) != 0) {
            x = flip->field_1D0;
        } else {
            x = flip->field_60;
        }
        pkt[9].field_0 = (((x >> 24) & 0x3F) << 20) |
                         ((unsigned long long)(unsigned int)(((unsigned int)customMode.field_0 >> 6) << 14) | 0xAA8000000ULL) |
                         (unsigned long long)(long long)(int)((unsigned int)(x & 0x1FF) << 5);
        pkt[9].field_8 = 0x06;
        // +0xA0 TEX1_1 = 0x61 (LCM 1, MMAG / MMIN linear); +0xB0 PRIM = 0x16 (sprite, TME); +0xC0 RGBAQ = Q 1.0
        pkt[10].field_0 = 0x61;
        pkt[10].field_8 = 0x14;
        pkt[11].field_0 = 0x16;
        pkt[11].field_8 = 0x00;
        pkt[12].field_0 = 0x3F80000000000000ULL;
        pkt[12].field_8 = 0x01;
        // +0xD0 ST = (A, t4 ? C : B) * 2^-14 through __floatdisf of the zero-extended ratio; +0xE0 XYZ2 = 0
        s = func_001FCA18_inl2(__floatdisf((long long)(unsigned long long)ratioA) * 0x1p-14f);
        if (t4 != 0) {
            t = func_001FCA18_inl2(__floatdisf((long long)(unsigned long long)ratioC) * 0x1p-14f);
        } else {
            t = func_001FCA18_inl2(__floatdisf((long long)(unsigned long long)ratioB) * 0x1p-14f);
        }
        pkt[13].field_0 = ((unsigned long long)t << 32) | (unsigned long long)s;
        pkt[13].field_8 = 0x02;
        pkt[14].field_0 = 0;
        pkt[14].field_8 = 0x05;
        // +0xF0 ST = (width * 16 + A, height * 16 + (t4 ? C : B)) * 2^-14 (32-bit sums converted as unsigned)
        u = ((unsigned int)customMode.field_0 << 4) + ratioA;
        s = func_001FCA18_inl2(func_001FCA18_inl1(u) * 0x1p-14f);
        if (t4 != 0) {
            u = ((unsigned int)customMode.field_4 << 4) + ratioC;
        } else {
            u = ((unsigned int)customMode.field_4 << 4) + ratioB;
        }
        t = func_001FCA18_inl2(func_001FCA18_inl1(u) * 0x1p-14f);
        pkt[15].field_0 = ((unsigned long long)t << 32) | (unsigned long long)s;
        pkt[15].field_8 = 0x02;
        // +0x100 XYZ2 = (display width << 4, display height << 4) (32-bit, sign-extended)
        pkt[16].field_0 = (unsigned long long)(long long)(int)(((unsigned int)customMode.field_1C << 20) |
                                                               ((unsigned int)customMode.field_18 << 4));
        pkt[16].field_8 = 0x05;
        // +0x110 XYOFFSET_1, +0x120 FRAME_1, +0x130 ZBUF_1 restored from the shadows
        pkt[17].field_0 = skyXyoffset_1;
        pkt[17].field_8 = 0x18;
        pkt[18].field_0 = flip->field_60;
        pkt[18].field_8 = 0x4C;
        pkt[19].field_0 = flip->field_70;
        pkt[19].field_8 = 0x4E;
        i++;
        // [C] 0x1BF: past this 0x140-byte buffer into the next [C 0x1FD098]
        SyncDCache(pkt, (char *)pkt + 0x1BF);
    } while (i < 4);
    // [C] stored 0x3CC, 0x3C0, 0x3C4, 0x3C8 [C 0x1FD0EC]
    flip->field_3CC = D_00675CC0;
    flip->field_3C0 = D_00675900;
    flip->field_3C4 = D_00675A40;
    flip->field_3C8 = D_00675B80;
}
