// RenderWare 3.6 sky2 driver: camera begin / end update, clear, show raster and the driver's matrix / point multiplies
// (RW 3.1 file name: driver/sky2/bacamera.c / skycam related; reading aid only).
// Written from the retail asm (decomp assembly/asm/rw/renderware.s). Tags: [C] confirmed from the asm, [L], [G].
// boot2 (track rw). The VU0 code is written as the decomp writes inline asm (decomp2host's eeasm translates it):
// explicit $vfN registers, the retail instruction order and dest masks, scratch GPRs by their retail names.

#include "b3hw/hw.h"
#include "rw/skyglobals.h"

// [C] a qword of GIF data in the scratchpad packet (words in memory order).
struct UnkStruct_skyCamQword {
    unsigned int field_0;
    unsigned int field_4;
    unsigned int field_8;
    unsigned int field_C;
};

// [C] _rwDMAFlipData read as 64-bit words (see basky_fb.cpp).
struct UnkStruct_skyCamFlip {
    unsigned long long q[0x7A];
};

// [C] a raster as the camera functions read it ([L] RwRaster).
struct UnkStruct_skyCamRaster {
    struct UnkStruct_skyCamRaster *field_0;   // [C] the parent raster ([L] parent)
    unsigned char pad_4[0x8];
    int field_C;                       // [C] width
    int field_10;                      // [C] height
    int field_14;                      // [C] depth
    unsigned char pad_18[0x4];
    short field_1C;                    // [C] nOffsetX
    short field_1E;                    // [C] nOffsetY
    unsigned char field_20;            // [C] type in bits 0-2 (5 = the camera's frame buffer kind here)
    unsigned char field_21;
    unsigned char pad_22[0x1];
    unsigned char field_23;            // [C] cFormat bits
};

// [C] the driver's raster extension at raster->field_0 + skyRasterExt.
struct UnkStruct_skyCamRasterExt {
    unsigned char pad_0[0x8];
    unsigned int field_8;              // [C] TBP0 etc. ([L] TEX0 low)
    unsigned char pad_C[0xB];
    unsigned char field_17;            // [C] nonzero when usable
};

// [C] a camera, the fields the driver reads ([L] RwCamera): +0x60 the frame buffer raster. The rest is read by
// offset (the frustum corner data at +0x124..+0x184 as floats).
struct UnkStruct_skyCamera {
    unsigned char pad_0[0x14];
    int field_14;                      // [C] projection (1 = perspective)
    unsigned char pad_18[0x8];
    unsigned char pad_20[0x40];
    UnkStruct_skyCamRaster *field_60;  // [C] the frame buffer raster ([L] frameBuffer)
};

// [C] customMode's first two words: the draw buffer's width / height.
struct UnkStruct_skyCamVideo {
    int field_0;
    int field_4;
};

// [C] RwError on the stack: {plugin ID 1, _rwerror(code)} ([L] RwError).
struct UnkStruct_skyCamError {
    int field_0;
    int field_4;
};

extern int skyCameraExt;                          // [C] gp 0x004E285C: the camera plugin's offset
extern int skyRasterExt;                          // [C] gp 0x004E2858: the raster plugin's offset
extern UnkStruct_skyCamVideo *skyVideoMode;       // [C] gp 0x004E1808
extern int skyRenderTimeInProgress;               // [C] gp 0x004E1828
extern unsigned long long skyRenderTime;          // [C] gp 0x004E1820
extern int skyFrameBit;                           // [C] gp 0x004E188C
extern int skyTexCacheRestoreRequired;            // [C] gp 0x004E18B4
extern unsigned long long skyFrame_1;             // [C] gp 0x004E1838 (FRAME_1 shadow)
extern unsigned long long skyZbuf_1;              // [C] gp 0x004E1830 (ZBUF_1 shadow)
extern unsigned long long skyTest_1;              // [C] gp 0x004E1840 (TEST_1 shadow)
extern unsigned long long skyXyoffset_1;          // [C] gp 0x004E1848 (XYOFFSET_1 shadow)
extern unsigned long long skyXOff;                // [C] gp 0x004E1890
extern unsigned long long skyYOff;                // [C] gp 0x004E1898
extern unsigned char skyUploadedCode;             // [C] gp 0x004E18D0 (a u32 symbol; the asm uses its first byte)
extern int D_004E28B0;                            // [C] gp 0x004E28B0
extern float D_004E28B4;                          // [C] gp 0x004E28B4
extern float D_004E0718;                          // [C] gp 0x004E0718
extern float D_004E0724;                          // [C] gp 0x004E0724
extern UnkStruct_skyCamFlip _rwDMAFlipData;       // [C] 0x006754C0
extern unsigned char skyProj[];                   // [C] 0x00675E00 (0x40 bytes: the camera's matrix rows)
extern float vu1DataNearClip[];                   // [C] 0x0049AF10 (4 floats)
extern float vu1DataFarClip[];                    // [C] 0x0049AF20
extern float xMaxYMax128[];                       // [C] 0x0049AF30
extern float vu1DataXYZScale[];                   // [C] 0x0049AF40
extern float vu1DataXYZShift[];                   // [C] 0x0049AF50
extern float vu1DataOffset3D[];                   // [C] 0x0049AF60
extern float vu1DataOffset2D[];                   // [C] 0x0049AFE0
extern float skyClipVect1[];                      // [C] 0x0049AED0
extern float skyClipVect2[];                      // [C] 0x0049AEE0
extern float skyCClipVect1[];                     // [C] 0x0049AEF0
extern float skyCClipVect2[];                     // [C] 0x0049AF00

extern "C" int _rwDMAOpenVIFPkt(unsigned int type, unsigned int nQW);
extern "C" void _rwDMAReqFlip(unsigned int flip, unsigned int frameBit);
extern "C" void func_001F43B0(void);
extern "C" void RpSkyTexCacheRestore(void);
extern "C" void _rpSkyFrameBufferRastersUpdate(void);
extern "C" void skyTexCacheEndFrame(void);
extern "C" void SkyGSMipGenerate(UnkStruct_skyCamRaster *raster);
extern "C" int _rwerror(int code);
extern "C" void *RwErrorSet(void *error);
extern "C" float _rwV3dNormalize(float *out, const float *in);
extern "C" void _rwSkyPipeSetupForCamera(UnkStruct_skyCamera *camera);
extern "C" float sqrtf(float x);

// [C] writes one A+D qword {data, reg} at the cursor and advances it by 0x10 (the asm's pcpyld + sq + addiu).
static void skyCamPutAD(unsigned long long data, unsigned int reg)
{
    UnkStruct_skyCamQword *q = (UnkStruct_skyCamQword *)farFogPlane;

    q->field_0 = (unsigned int)data;
    q->field_4 = (unsigned int)(data >> 32);
    q->field_8 = reg;
    q->field_C = 0;
    farFogPlane = farFogPlane + 0x10;
}

// [C] the ISR-byte test the XYOFFSET update uses: with interrupts disabled (the di / EIE loop) the bit-35 flag =
// _rwDMAAsyncPktPtr[1] ^ (_rwDMAAsyncPktPtr[2] & 1) ^ _rwDMAAsyncPktPtr[3] << 35; interrupts are re-enabled.
static unsigned long long skyCamIsrBits(void)
{
    unsigned int v1;
    unsigned int v0;
    unsigned int a0;

    do {
        b3hw_di();
        // sync.p
    } while (b3hw_eie());
    v1 = _rwDMAAsyncPktPtr[2];
    v0 = _rwDMAAsyncPktPtr[1];
    v1 = v1 & 1;
    a0 = _rwDMAAsyncPktPtr[3];
    v0 = v0 ^ v1;
    v0 = v0 ^ a0;
    b3hw_ei();
    return (unsigned long long)v0 << 35;
}

// [C] FRAME_1 for the camera's raster: for a raster of type 5 (bits 0-2 of +0x20) built from the extension word +8
// ((w >> 5) & 0x1FF | (w << 2) & 0x3F0000 | (w << 4) & 0x3F000000; the extension must have its +0x17 byte set, else
// returns 0), else the flip data word 0x60 / 8 (frame bit set) or 0x1D0 / 8 (clear); stored in skyFrame_1.
static int skyCamSetFrame1(UnkStruct_skyCamRaster *raster)
{
    UnkStruct_skyCamRasterExt *ext;
    unsigned int w;

    if ((raster->field_20 & 7) == 5) {
        ext = (UnkStruct_skyCamRasterExt *)((unsigned char *)raster->field_0 + skyRasterExt);
        if (ext->field_17 == 0) {
            return 0;
        }
        w = ext->field_8;
        skyFrame_1 = (unsigned long long)(((w >> 5) & 0x1FF) | ((w << 2) & 0x3F0000) | ((w << 4) & 0x3F000000));
    } else if ((skyFrameBit & 1) != 0) {
        skyFrame_1 = _rwDMAFlipData.q[0x60 / 8];
    } else {
        skyFrame_1 = _rwDMAFlipData.q[0x1D0 / 8];
    }
    return 1;
}

static float skyCamF(unsigned char *p, int off)
{
    return *(float *)(p + off);
}

static void skyCamSetF(unsigned char *p, int off, float v)
{
    *(float *)(p + off) = v;
}

// [C] n = a x b stored as three floats at dst (x, y, z): x = ay*bz - az*by, y = az*bx - ax*bz, z = ax*by - ay*bx
// (the products first, then the three subtractions; stores z, y, x).
static void skyCamCross(unsigned char *dst, float ax, float ay, float az, float bx, float by, float bz)
{
    float f0, f2, f4, f5, f6, f7;

    f7 = ay * bz;
    f5 = ay * bx;
    f2 = ax * by;
    f0 = az * bx;
    f6 = ax * bz;
    f4 = az * by;
    f2 = f2 - f5;
    f0 = f0 - f6;
    f7 = f7 - f4;
    skyCamSetF(dst, 8, f2);
    skyCamSetF(dst, 4, f0);
    skyCamSetF(dst, 0, f7);
}

// [C] after the normal at e + nOff has been normalized: the plane distance (v.x * nx + v.y * ny) + v.z * nz (v = e + vOff,
// 0xC or 0x24) at e + nOff + 0xC and the three sign bytes at e + nOff + 0x10 .. 0x12, each (bits >> 31) + 1 (1 for a
// non-negative float, 0 for a negative one).
static void skyCamPlane(unsigned char *e, int vOff, int nOff)
{
    float f0, f1, f2, f3, f4;
    int a0, v0, v1;

    f1 = skyCamF(e, vOff);
    f2 = skyCamF(e, vOff + 4);
    f0 = skyCamF(e, nOff);
    f3 = skyCamF(e, nOff + 4);
    f1 = f1 * f0;
    f4 = skyCamF(e, vOff + 8);
    f2 = f2 * f3;
    f0 = skyCamF(e, nOff + 8);
    a0 = *(int *)(e + nOff);
    f4 = f4 * f0;
    v0 = *(int *)(e + nOff + 4);
    f1 = f1 + f2;
    v1 = *(int *)(e + nOff + 8);
    a0 = (a0 >> 31) + 1;
    v0 = (v0 >> 31) + 1;
    v1 = (v1 >> 31) + 1;
    f1 = f1 + f4;
    e[nOff + 0x10] = (unsigned char)a0;
    e[nOff + 0x11] = (unsigned char)v0;
    skyCamSetF(e, nOff + 0xC, f1);
    e[nOff + 0x12] = (unsigned char)v1;
}

// [C] the camera begin-update of the sky2 driver (the standard camera-begin function: a1 = the camera; a0 unused).
// 1. The frame buffer raster (+0x60) must fit the video mode (width <= skyVideoMode[0], height <= [1]), else error
//    {1, _rwerror(0x80000010)} and 0. 2. func_001F43B0(); skyRenderTimeInProgress = 1 when it was 0. 3. FRAME_1 shadow
//    (skyCamSetFrame1; 0 returned when the extension is not usable). 4. RpSkyTexCacheRestore when
//    skyTexCacheRestoreRequired. 5. A VIF packet of 10 qwords (type bit 31): GIFtag NLOOP 9, then A+D: XYOFFSET_1 / _2
//    (0x18 / 0x19), SCISSOR_1 / _2 (0x40 / 0x41), TEST_1 / _2 (0x47 / 0x48), DTHE (0x45: raster depth < 0x18),
//    FRAME_1 / _2 (0x4C / 0x4D). The XYOFFSET is, for a type-5 raster, ((0x800 - h/2) << 36) | ((0x800 - w/2) << 4) of
//    the parent raster, else skyXOff << 4 | skyYOff << 36 plus, when _rwDMAAsyncPktPtr[0] is set, the ISR bit flag
//    << 35; stored in skyXyoffset_1. 6. _rwSkyPipeSetupForCamera(camera). 7. The view-volume data in the camera extension
//    (camera + skyCameraExt): the two corner sets (camera +0x124.. and +0x154..) scaled by the raster's half-size
//    factors give 24 floats at e + 0 .. 0x5C, the two 20-byte blocks (camera +0x94 / +0xA8) are copied to e + 0x60 and
//    e + 0x74, four plane normals (cross products normalized with _rwV3dNormalize) with their distances and sign bytes
//    follow at e + 0x88.., and the camera's four matrix rows (+0x20..+0x5F) are copied to skyProj with skyProjState
//    incremented. Returns 1.
// 0x001F5708
extern "C" int SkyCameraBeginUpdate(void *out, UnkStruct_skyCamera *camera)
{
    unsigned char *c = (unsigned char *)camera;
    unsigned char *e;
    UnkStruct_skyCamVideo *vm;
    UnkStruct_skyCamRaster *raster;
    UnkStruct_skyCamRaster *t1;
    UnkStruct_skyCamError err;
    unsigned long long xy;
    unsigned long long sc;
    int v0;
    int v1;
    float f0, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10;
    float ax, ay, az, bx, by, bz;
    unsigned int w0, w1, w2, w3, w4, w5;

    vm = skyVideoMode;
    e = c + skyCameraExt;
    raster = camera->field_60;
    if (vm->field_0 < raster->field_C || vm->field_4 < raster->field_10) {
        err.field_0 = 1;
        err.field_4 = _rwerror(0x80000010);
        RwErrorSet(&err);
        return 0;
    }
    func_001F43B0();
    if (skyRenderTimeInProgress == 0) {
        skyRenderTimeInProgress = 1;
    }
    raster = camera->field_60;
    if (!skyCamSetFrame1(raster)) {
        return 0;
    }
    if (skyTexCacheRestoreRequired != 0) {
        RpSkyTexCacheRestore();
        skyTexCacheRestoreRequired = 0;
    }
    _rwDMAOpenVIFPkt(0x80000000, 0xA);
    skyCamPutAD(((unsigned long long)0x10000000 << 32) | 0x8009, 0xE);   // GIFtag NLOOP 9, EOP, NREG 1, A+D
    raster = camera->field_60;
    if ((raster->field_20 & 7) == 5) {
        t1 = raster->field_0;
        xy = ((unsigned long long)(long long)(0x800 - (t1->field_10 >> 1)) << 36) |
             ((unsigned long long)(long long)(0x800 - (t1->field_C >> 1)) << 4);
        skyXyoffset_1 = xy;
    } else {
        xy = (skyXOff << 4) | (skyYOff << 36);
        if (_rwDMAAsyncPktPtr[0] != 0) {
            xy = xy | skyCamIsrBits();
        }
        skyXyoffset_1 = xy;
    }
    xy = skyXyoffset_1;
    skyCamPutAD(xy, 0x18);
    skyCamPutAD(xy, 0x19);
    t1 = camera->field_60;
    w0 = (unsigned short)t1->field_1C;
    w1 = (unsigned short)t1->field_1E;
    v0 = t1->field_C + (int)(short)w0 - 1;
    v1 = t1->field_10 + (int)(short)w1;
    sc = (unsigned long long)(long long)(int)((unsigned int)v0 << 16) | (unsigned long long)(w0 & 0x7FF) |
         ((unsigned long long)(w1 & 0x7FF) << 32) | ((unsigned long long)(long long)(v1 - 1) << 48);
    skyCamPutAD(sc, 0x40);
    skyCamPutAD(sc, 0x41);
    xy = skyTest_1;
    skyCamPutAD(xy, 0x47);
    skyCamPutAD(xy, 0x48);
    skyCamPutAD((unsigned long long)((unsigned int)t1->field_14 < 0x18u), 0x45);
    xy = skyFrame_1;
    skyCamPutAD(xy, 0x4C);
    skyCamPutAD(xy, 0x4D);
    _rwSkyPipeSetupForCamera(camera);
    raster = camera->field_60;
    f9 = D_004E0718;
    v0 = raster->field_C >> 1;
    v1 = raster->field_10 >> 1;
    f3 = 1.0f;
    f0 = (float)v0;
    f1 = (float)v1;
    f2 = 0.5f;
    f10 = f9 - f0;
    f9 = f9 - f1;
    f10 = f10 / f0;
    f9 = f9 / f1;
    f10 = f10 - f3;
    f9 = f9 - f3;
    f10 = f10 * f2;
    f9 = f9 * f2;
    f4 = skyCamF(c, 0x130);
    f7 = skyCamF(c, 0x13C);
    f5 = skyCamF(c, 0x134);
    f8 = skyCamF(c, 0x140);
    f4 = f4 - f7;
    f6 = skyCamF(c, 0x138);
    f3 = skyCamF(c, 0x144);
    f5 = f5 - f8;
    f6 = f6 - f3;
    f0 = skyCamF(c, 0x148);
    f2 = skyCamF(c, 0x14C);
    f0 = f0 - f7;
    f1 = skyCamF(c, 0x150);
    f2 = f2 - f8;
    f1 = f1 - f3;
    f4 = f4 * f9;
    f5 = f5 * f9;
    f6 = f6 * f9;
    f0 = f0 * f10;
    f2 = f2 * f10;
    f1 = f1 * f10;
    f3 = f4 + f0;
    f7 = f5 + f2;
    f8 = f6 + f1;
    f4 = f4 - f0;
    f5 = f5 - f2;
    f6 = f6 - f1;
    skyCamSetF(e, 0x0, skyCamF(c, 0x124) + f3);
    skyCamSetF(e, 0x4, skyCamF(c, 0x128) + f7);
    skyCamSetF(e, 0x8, skyCamF(c, 0x12C) + f8);
    skyCamSetF(e, 0xC, skyCamF(c, 0x130) + f4);
    skyCamSetF(e, 0x10, skyCamF(c, 0x134) + f5);
    skyCamSetF(e, 0x14, skyCamF(c, 0x138) + f6);
    skyCamSetF(e, 0x18, skyCamF(c, 0x13C) - f3);
    skyCamSetF(e, 0x1C, skyCamF(c, 0x140) - f7);
    skyCamSetF(e, 0x20, skyCamF(c, 0x144) - f8);
    skyCamSetF(e, 0x24, skyCamF(c, 0x148) - f4);
    skyCamSetF(e, 0x28, skyCamF(c, 0x14C) - f5);
    skyCamSetF(e, 0x2C, skyCamF(c, 0x150) - f6);
    f4 = skyCamF(c, 0x160);
    f7 = skyCamF(c, 0x16C);
    f5 = skyCamF(c, 0x164);
    f8 = skyCamF(c, 0x170);
    f4 = f4 - f7;
    f6 = skyCamF(c, 0x168);
    f3 = skyCamF(c, 0x174);
    f5 = f5 - f8;
    f6 = f6 - f3;
    f0 = skyCamF(c, 0x178);
    f1 = skyCamF(c, 0x17C);
    f0 = f0 - f7;
    f2 = skyCamF(c, 0x180);
    f1 = f1 - f8;
    f2 = f2 - f3;
    f4 = f4 * f9;
    f6 = f6 * f9;
    f5 = f5 * f9;
    f0 = f0 * f10;
    f2 = f2 * f10;
    f1 = f1 * f10;
    f3 = f4 + f0;
    f7 = f5 + f1;
    f8 = f6 + f2;
    f4 = f4 - f0;
    f5 = f5 - f1;
    f6 = f6 - f2;
    skyCamSetF(e, 0x30, skyCamF(c, 0x154) + f3);
    skyCamSetF(e, 0x34, skyCamF(c, 0x158) + f7);
    skyCamSetF(e, 0x38, skyCamF(c, 0x15C) + f8);
    skyCamSetF(e, 0x3C, skyCamF(c, 0x160) + f4);
    skyCamSetF(e, 0x40, skyCamF(c, 0x164) + f5);
    skyCamSetF(e, 0x44, skyCamF(c, 0x168) + f6);
    skyCamSetF(e, 0x48, skyCamF(c, 0x16C) - f3);
    skyCamSetF(e, 0x4C, skyCamF(c, 0x170) - f7);
    skyCamSetF(e, 0x50, skyCamF(c, 0x174) - f8);
    skyCamSetF(e, 0x54, skyCamF(c, 0x178) - f4);
    skyCamSetF(e, 0x58, skyCamF(c, 0x17C) - f5);
    skyCamSetF(e, 0x5C, skyCamF(c, 0x180) - f6);
    // [C 0x1F5CD0] two 20-byte blocks of the camera copied to the extension (+0x60, +0x74)
    {
        unsigned int *src = (unsigned int *)(c + 0x94);
        unsigned int *dst = (unsigned int *)(e + 0x60);

        w0 = src[0];
        w1 = src[1];
        w2 = src[2];
        w3 = src[3];
        w4 = src[4];
        dst[0] = w0;
        dst[1] = w1;
        dst[2] = w2;
        dst[3] = w3;
        dst[4] = w4;
        src = (unsigned int *)(c + 0xA8);
        dst = (unsigned int *)(e + 0x74);
        w0 = src[0];
        w1 = src[1];
        w2 = src[2];
        w3 = src[3];
        w4 = src[4];
        dst[0] = w0;
        dst[1] = w1;
        dst[2] = w2;
        dst[3] = w3;
        dst[4] = w4;
    }
    // plane 1: (e[0xC..] - e[0x3C..]) x (e[0x48..] - e[0x3C..])
    f6 = skyCamF(e, 0xC);
    f7 = skyCamF(e, 0x3C);
    f5 = skyCamF(e, 0x10);
    f8 = skyCamF(e, 0x40);
    f6 = f6 - f7;
    f4 = skyCamF(e, 0x14);
    f2 = skyCamF(e, 0x44);
    f5 = f5 - f8;
    ax = f6;
    f4 = f4 - f2;
    ay = f5;
    az = f4;
    f0 = skyCamF(e, 0x48);
    f3 = skyCamF(e, 0x4C);
    f0 = f0 - f7;
    f1 = skyCamF(e, 0x50);
    f3 = f3 - f8;
    f1 = f1 - f2;
    skyCamCross(e + 0x88, ax, ay, az, f0, f3, f1);
    _rwV3dNormalize((float *)(e + 0x88), (float *)(e + 0x88));
    skyCamPlane(e, 0xC, 0x88);
    // plane 2: (e[0x30..] - e[0x3C..]) x a
    f3 = skyCamF(e, 0x30);
    f0 = skyCamF(e, 0x3C);
    f2 = skyCamF(e, 0x34);
    f3 = f3 - f0;
    f4 = skyCamF(e, 0x40);
    f1 = skyCamF(e, 0x38);
    f0 = skyCamF(e, 0x44);
    f2 = f2 - f4;
    f1 = f1 - f0;
    skyCamCross(e + 0x9C, f3, f2, f1, ax, ay, az);
    _rwV3dNormalize((float *)(e + 0x9C), (float *)(e + 0x9C));
    skyCamPlane(e, 0xC, 0x9C);
    // plane 3: (e[0x24..] - e[0x54..]) x (e[0x30..] - e[0x54..])
    f6 = skyCamF(e, 0x24);
    f7 = skyCamF(e, 0x54);
    f5 = skyCamF(e, 0x28);
    f8 = skyCamF(e, 0x58);
    f6 = f6 - f7;
    f4 = skyCamF(e, 0x2C);
    f2 = skyCamF(e, 0x5C);
    f5 = f5 - f8;
    ax = f6;
    f4 = f4 - f2;
    ay = f5;
    az = f4;
    f0 = skyCamF(e, 0x30);
    f3 = skyCamF(e, 0x34);
    f0 = f0 - f7;
    f1 = skyCamF(e, 0x38);
    f3 = f3 - f8;
    f1 = f1 - f2;
    skyCamCross(e + 0xB0, ax, ay, az, f0, f3, f1);
    _rwV3dNormalize((float *)(e + 0xB0), (float *)(e + 0xB0));
    skyCamPlane(e, 0x24, 0xB0);
    // plane 4: (e[0x48..] - e[0x54..]) x a
    f3 = skyCamF(e, 0x48);
    f0 = skyCamF(e, 0x54);
    f2 = skyCamF(e, 0x4C);
    f3 = f3 - f0;
    f4 = skyCamF(e, 0x58);
    f1 = skyCamF(e, 0x50);
    f0 = skyCamF(e, 0x5C);
    f2 = f2 - f4;
    f1 = f1 - f0;
    skyCamCross(e + 0xC4, f3, f2, f1, ax, ay, az);
    _rwV3dNormalize((float *)(e + 0xC4), (float *)(e + 0xC4));
    skyCamPlane(e, 0x24, 0xC4);
    // the camera's matrix rows to skyProj, and the change counter
    {
        unsigned int *src = (unsigned int *)(c + 0x20);
        unsigned int *dst = (unsigned int *)skyProj;
        int k;
        int cnt = skyProjState;

        for (k = 0; k < 16; k++) {
            dst[k] = src[k];
        }
        skyProjState = cnt + 1;
    }
    return 1;
}

// [C] (VU0) dst = a * b for 4x4 float matrices (rows of 4 floats at +0, +0x10, +0x20, +0x30): rows of a are loaded to
// $vf1..$vf4, rows of b to $vf5..$vf8 and the first row of dst to $vf13 (its w is kept for result row 0:
// vmove.w $vf9, $vf13); result row i (xyz) = a[i].x * b[0] + a[i].y * b[1] + a[i].z * b[2], plus b[3] for row 3
// (vmaddw with $vf0.w = 1); the w lane of rows 1..3 is whatever $vf10..$vf12 held ([G] left from earlier use: the asm
// does not write it). The four rows are stored to dst. No return value ($a0 is left alone).
// 0x001FFE18
extern "C" void func_001FFE18(float *dst, const float *a, const float *b)
{
    asm volatile(
        "lqc2 $vf1, 0x0(%1)" "\n\t"
        "lqc2 $vf2, 0x10(%1)" "\n\t"
        "lqc2 $vf3, 0x20(%1)" "\n\t"
        "lqc2 $vf4, 0x30(%1)" "\n\t"
        "lqc2 $vf5, 0x0(%2)" "\n\t"
        "lqc2 $vf6, 0x10(%2)" "\n\t"
        "lqc2 $vf7, 0x20(%2)" "\n\t"
        "lqc2 $vf8, 0x30(%2)" "\n\t"
        "lqc2 $vf13, 0x0(%0)" "\n\t"
        "vmulax.xyz $ACC, $vf5, $vf1x" "\n\t"
        "vmadday.xyz $ACC, $vf6, $vf1y" "\n\t"
        "vmaddz.xyz $vf9, $vf7, $vf1z" "\n\t"
        "vmulax.xyz $ACC, $vf5, $vf2x" "\n\t"
        "vmadday.xyz $ACC, $vf6, $vf2y" "\n\t"
        "vmaddz.xyz $vf10, $vf7, $vf2z" "\n\t"
        "vmove.w $vf9, $vf13" "\n\t"
        "vmulax.xyz $ACC, $vf5, $vf3x" "\n\t"
        "vmadday.xyz $ACC, $vf6, $vf3y" "\n\t"
        "vmaddz.xyz $vf11, $vf7, $vf3z" "\n\t"
        "vmulax.xyz $ACC, $vf5, $vf4x" "\n\t"
        "vmadday.xyz $ACC, $vf6, $vf4y" "\n\t"
        "vmaddaz.xyz $ACC, $vf7, $vf4z" "\n\t"
        "vmaddw.xyz $vf12, $vf8, $vf0w" "\n\t"
        "sqc2 $vf9, 0x0(%0)" "\n\t"
        "sqc2 $vf10, 0x10(%0)" "\n\t"
        "sqc2 $vf11, 0x20(%0)" "\n\t"
        "sqc2 $vf12, 0x30(%0)"
        : : "r"(dst), "r"(a), "r"(b) : "memory");
}

// [C] (VU0) out = in * M for one point: the matrix rows (a2) go to $vf1..$vf4, the point's three words are read with
// lwu, joined (x | y << 32, then pcpyld with z) and moved to $vf5 (w = 0, qmtc2.ni); xyz = in.x * row0 + in.y * row1 +
// in.z * row2 + row3 (the translation, vmaddw with $vf0.w); the result is moved back (qmfc2.ni) and its x, y, z are
// stored as three words at out. Returns out.
// 0x001FFE90
extern "C" float *func_001FFE90(float *out, const float *in, const float *m)
{
    asm volatile(
        "lqc2 $vf1, 0x0(%2)" "\n\t"
        "lqc2 $vf2, 0x10(%2)" "\n\t"
        "lqc2 $vf3, 0x20(%2)" "\n\t"
        "lqc2 $vf4, 0x30(%2)" "\n\t"
        "lwu $v1, 0x0(%1)" "\n\t"
        "lwu $at, 0x4(%1)" "\n\t"
        "lwu $v0, 0x8(%1)" "\n\t"
        "dsll32 $at, $at, 0" "\n\t"
        "or $at, $at, $v1" "\n\t"
        "pcpyld $v0, $v0, $at" "\n\t"
        "qmtc2.ni $v0, $vf5" "\n\t"
        "vmulax.xyz $ACC, $vf1, $vf5x" "\n\t"
        "vmadday.xyz $ACC, $vf2, $vf5y" "\n\t"
        "vmaddaz.xyz $ACC, $vf3, $vf5z" "\n\t"
        "vmaddw.xyz $vf5, $vf4, $vf0w" "\n\t"
        "qmfc2.ni $at, $vf5" "\n\t"
        "dsrl32 $v0, $at, 0" "\n\t"
        "pcpyud $v1, $at, $at" "\n\t"
        "sw $at, 0x0(%0)" "\n\t"
        "sw $v0, 0x4(%0)" "\n\t"
        "sw $v1, 0x8(%0)"
        : : "r"(out), "r"(in), "r"(m) : "memory");
    return out;
}

// [C] (VU0) the same transform for a0 = out, a1 = in, a2 = count points (stride 0xC for both), matrix a3. The matrix
// rows go to $vf1..$vf4 and the first point to $vf5; then, while count > 0: the input pointer is advanced by 0xC, the
// current point ($vf5) is multiplied into $vf6 (same accumulate sequence), the NEXT point (read from the advanced
// pointer, so the last pass reads one point past the end) is moved to $vf5, count is decremented, $vf6 is read back
// and stored as three words at out, out advanced by 0xC. Returns the original out.
// 0x001FFEF0
extern "C" float *func_001FFEF0(float *out, const float *in, int count, const float *m)
{
    float *o = out;
    const float *p = in;

    asm volatile(
        "lqc2 $vf1, 0x0(%1)" "\n\t"
        "lqc2 $vf2, 0x10(%1)" "\n\t"
        "lqc2 $vf3, 0x20(%1)" "\n\t"
        "lqc2 $vf4, 0x30(%1)" "\n\t"
        "lwu $t5, 0x0(%0)" "\n\t"
        "lwu $t3, 0x4(%0)" "\n\t"
        "lwu $t4, 0x8(%0)" "\n\t"
        "dsll32 $t3, $t3, 0" "\n\t"
        "or $t3, $t3, $t5" "\n\t"
        "pcpyld $t4, $t4, $t3" "\n\t"
        "qmtc2.ni $t4, $vf5"
        : : "r"(p), "r"(m) : "memory");
    for (;;) {
        if (count <= 0) {
            break;
        }
        p = p + 3;
        asm volatile(
            "vmulax.xyz $ACC, $vf1, $vf5x" "\n\t"
            "vmadday.xyz $ACC, $vf2, $vf5y" "\n\t"
            "vmaddaz.xyz $ACC, $vf3, $vf5z" "\n\t"
            "vmaddw.xyz $vf6, $vf4, $vf0w" "\n\t"
            "lwu $t5, 0x0(%0)" "\n\t"
            "lwu $t3, 0x4(%0)" "\n\t"
            "lwu $t4, 0x8(%0)"
            : : "r"(p) : "memory");
        count = count - 1;
        asm volatile(
            "dsll32 $t3, $t3, 0" "\n\t"
            "or $t3, $t3, $t5" "\n\t"
            "pcpyld $t4, $t4, $t3" "\n\t"
            "qmtc2.ni $t4, $vf5" "\n\t"
            "qmfc2.ni $t9, $vf6" "\n\t"
            "dsrl32 $v0, $t9, 0" "\n\t"
            "pcpyud $v1, $t9, $t9" "\n\t"
            "sw $t9, 0x0(%0)" "\n\t"
            "sw $v0, 0x4(%0)" "\n\t"
            "sw $v1, 0x8(%0)"
            : : "r"(o) : "memory");
        o = o + 3;
    }
    return out;
}

// [C] (VU0) out = in * M's 3x3 part for one vector (no translation): rows 0..2 (a2) in $vf1..$vf3; the point as above
// in $vf4 (w = 0); xyz = x * row0 + y * row1 + z * row2 (the last step writes $vf4 directly, vmaddz); stored as three
// words at out. Returns out.
// 0x001FFF80
extern "C" float *func_001FFF80(float *out, const float *in, const float *m)
{
    asm volatile(
        "lqc2 $vf1, 0x0(%2)" "\n\t"
        "lqc2 $vf2, 0x10(%2)" "\n\t"
        "lqc2 $vf3, 0x20(%2)" "\n\t"
        "lwu $at, 0x0(%1)" "\n\t"
        "lwu $v0, 0x4(%1)" "\n\t"
        "lwu $v1, 0x8(%1)" "\n\t"
        "dsll32 $v0, $v0, 0" "\n\t"
        "or $v0, $v0, $at" "\n\t"
        "pcpyld $v1, $v1, $v0" "\n\t"
        "qmtc2.ni $v1, $vf4" "\n\t"
        "vmulax.xyz $ACC, $vf1, $vf4x" "\n\t"
        "vmadday.xyz $ACC, $vf2, $vf4y" "\n\t"
        "vmaddz.xyz $vf4, $vf3, $vf4z" "\n\t"
        "qmfc2.ni $at, $vf4" "\n\t"
        "dsrl32 $v0, $at, 0" "\n\t"
        "pcpyud $v1, $at, $at" "\n\t"
        "sw $at, 0x0(%0)" "\n\t"
        "sw $v0, 0x4(%0)" "\n\t"
        "sw $v1, 0x8(%0)"
        : : "r"(out), "r"(in), "r"(m) : "memory");
    return out;
}

// [C] (VU0) func_001FFEF0 without the translation: a0 = out, a1 = in, a2 = count, a3 = matrix (rows 0..2 in
// $vf1..$vf3); per point the product goes to $vf6 (vmaddz) and the next point is read ahead into $vf5 (one past the end
// on the last pass), as in func_001FFEF0. Returns the original out.
// 0x001FFFD8
extern "C" float *func_001FFFD8(float *out, const float *in, int count, const float *m)
{
    float *o = out;
    const float *p = in;

    asm volatile(
        "lqc2 $vf1, 0x0(%1)" "\n\t"
        "lqc2 $vf2, 0x10(%1)" "\n\t"
        "lqc2 $vf3, 0x20(%1)" "\n\t"
        "lwu $t8, 0x0(%0)" "\n\t"
        "lwu $t6, 0x4(%0)" "\n\t"
        "lwu $t7, 0x8(%0)" "\n\t"
        "dsll32 $t6, $t6, 0" "\n\t"
        "or $t6, $t6, $t8" "\n\t"
        "pcpyld $t7, $t7, $t6" "\n\t"
        "qmtc2.ni $t7, $vf5"
        : : "r"(p), "r"(m) : "memory");
    for (;;) {
        if (count <= 0) {
            break;
        }
        p = p + 3;
        asm volatile(
            "vmulax.xyz $ACC, $vf1, $vf5x" "\n\t"
            "vmadday.xyz $ACC, $vf2, $vf5y" "\n\t"
            "vmaddz.xyz $vf6, $vf3, $vf5z" "\n\t"
            "lwu $t8, 0x0(%0)" "\n\t"
            "lwu $t6, 0x4(%0)" "\n\t"
            "lwu $t7, 0x8(%0)"
            : : "r"(p) : "memory");
        count = count - 1;
        asm volatile(
            "dsll32 $t6, $t6, 0" "\n\t"
            "or $t6, $t6, $t8" "\n\t"
            "pcpyld $t7, $t7, $t6" "\n\t"
            "qmtc2.ni $t7, $vf5" "\n\t"
            "qmfc2.ni $t9, $vf6" "\n\t"
            "dsrl32 $v0, $t9, 0" "\n\t"
            "pcpyud $v1, $t9, $t9" "\n\t"
            "sw $t9, 0x0(%0)" "\n\t"
            "sw $v0, 0x4(%0)" "\n\t"
            "sw $v1, 0x8(%0)"
            : : "r"(o) : "memory");
        o = o + 3;
    }
    return out;
}

// [C] the camera end-update (a1 = the camera; a0 unused). For a type-5 frame buffer raster (+0x20 & 7): a VIF packet of
// 2 qwords (type bit 31): the GIFtag {NLOOP 1, EOP, A+D} and TEXFLUSH (0x3F); when the raster's +0x23 & 0x90 is 0x90
// SkyGSMipGenerate(raster). Then skyRenderTimeInProgress is cleared when it was set. Returns 1.
// 0x001FFA00
extern "C" int SkyCameraEndUpdate(void *out, UnkStruct_skyCamera *camera)
{
    UnkStruct_skyCamRaster *raster = camera->field_60;
    int busy;

    if ((raster->field_20 & 7) == 5) {
        _rwDMAOpenVIFPkt(0x80000000, 2);
        skyCamPutAD(((unsigned long long)0x10000000 << 32) | 0x8001, 0xE);
        skyCamPutAD(0, 0x3F);
        raster = camera->field_60;
        if ((raster->field_23 & 0x90) == 0x90) {
            SkyGSMipGenerate(raster);
        }
        busy = skyRenderTimeInProgress;
    } else {
        busy = skyRenderTimeInProgress;
    }
    if (busy != 0) {
        skyRenderTimeInProgress = 0;
    }
    return 1;
}

// [C] the show-raster function: when skyRenderTime (64 bits) is nonzero it is cleared together with
// skyRenderTimeInProgress; skyTexCacheEndFrame(); _rwDMAReqFlip(&_rwDMAFlipData, skyFrameBit & 0xFF) with skyFrameBit
// incremented (stored before the call); _rpSkyFrameBufferRastersUpdate(); skyTexCacheRestoreRequired = 1 when any of the
// words at flip data + 0x3C0 / 0x3C4 / 0x3C8 / 0x3CC is nonzero. Returns 1 (the arguments are not used).
// 0x001FFAB8
extern "C" int SkyRasterShowRaster(void *a0, void *a1, unsigned int a2)
{
    unsigned int bit;
    unsigned int *w;

    if (skyRenderTime != 0) {
        skyRenderTime = 0;
        skyRenderTimeInProgress = 0;
    }
    skyTexCacheEndFrame();
    bit = (unsigned int)skyFrameBit;
    skyFrameBit = (int)(bit + 1);
    _rwDMAReqFlip((unsigned int)&_rwDMAFlipData, bit & 0xFF);
    _rpSkyFrameBufferRastersUpdate();
    w = (unsigned int *)&_rwDMAFlipData;
    if (w[0x3C0 / 4] != 0 || w[0x3C4 / 4] != 0 || w[0x3C8 / 4] != 0 || w[0x3CC / 4] != 0) {
        skyTexCacheRestoreRequired = 1;
    }
    return 1;
}

// [C] the camera clear (a0 = the camera, a1 = the clear colour RGBA bytes, a2 = flags: bit 0 colour, bit 1 depth).
// func_001F43B0(); FRAME_1 shadow (skyCamSetFrame1; 0 returned when the extension is not usable). With
// n = ((width + offX + 31) & ~31) - (short)(offX & ~31) >> 5 32-pixel strips a VIF packet of 3n + 12 qwords (type bit
// 31) is opened: GIFtag NLOOP 3n + 11 (EOP, A+D), FRAME_1 (0x4C; without bit 0 of the flags the frame mask word is
// 0xFFFFFFFF, i.e. no colour write), ZBUF_1 (0x4E; with bit 1 clear ZMSK is set), DTHE (0x45: depth < 0x18), TEST_1
// (0x47: skyTest_1 & ~0x54001 | 0x30000: always pass), XYOFFSET_1 = 0, SCISSOR_1 = 0x7FFF00007FFF0000, PRIM sprite (0, 6);
// then per strip RGBAQ-less sprite corners: XYZ2 (x << 4 | offY << 20), RGBAQ (the packed colour with its alpha scaled
// by D_004E0724 and truncated, or 0 without bit 0) and XYZ2 of the strip's far corner (clipped to the raster width, y
// = offY + height); finally FRAME_1 and ZBUF_1 restored from their shadows and XYOFFSET_1 / _2 written from
// skyXyoffset_1 with bit 35 cleared (and the ISR bit flag, when _rwDMAAsyncPktPtr[0] is set, ORed in). Returns 1.
// 0x001F6108
extern "C" int SkyCameraClear(UnkStruct_skyCamera *camera, const unsigned char *color, unsigned int flags)
{
    UnkStruct_skyCamRaster *raster;
    UnkStruct_skyCamRaster *t0;
    unsigned long long data;
    unsigned long long xy;
    int s0;
    int a1;
    int v0;
    int t2;
    int right;
    int x2;
    int offX;
    float f0;
    int col;
    unsigned int a;

    func_001F43B0();
    raster = camera->field_60;
    if (!skyCamSetFrame1(raster)) {
        return 0;
    }
    raster = camera->field_60;
    offX = raster->field_1C;
    s0 = raster->field_C;
    v0 = (int)(short)((unsigned short)raster->field_1C & 0xFFE0);
    s0 = s0 + offX;
    s0 = s0 + 0x1F;
    s0 = s0 & -0x20;
    s0 = s0 - v0;
    s0 = s0 >> 5;
    a1 = s0 * 3;
    _rwDMAOpenVIFPkt(0x80000000, (unsigned int)(a1 + 0xC));
    skyCamPutAD(((unsigned long long)0x10000000 << 32) | (unsigned long long)(long long)(s0 * 3 + 0xB) | 0x8000, 0xE);
    if ((flags & 1) != 0) {
        data = (unsigned int)skyFrame_1;
    } else {
        data = skyFrame_1 | 0xFFFFFFFF00000000ULL;
    }
    skyCamPutAD(data, 0x4C);
    if ((flags & 2) != 0) {
        data = skyZbuf_1 & 0xFFFFFFFEFFFFFFFFULL;
    } else {
        data = skyZbuf_1 | 0x100000000ULL;
    }
    skyCamPutAD(data, 0x4E);
    raster = camera->field_60;
    skyCamPutAD((unsigned long long)((unsigned int)raster->field_14 < 0x18u), 0x45);
    data = (skyTest_1 & 0xFFFFFFFFFFFABFFEULL) | 0x30000ULL;
    skyCamPutAD(data, 0x47);
    skyCamPutAD(0, 0x18);
    skyCamPutAD(0x7FFF00007FFF0000ULL, 0x40);
    skyCamPutAD(6, 0);
    t2 = raster->field_1C;
    right = raster->field_C + t2;
    if (t2 < right) {
        do {
            data = (unsigned long long)(long long)(int)(((unsigned int)(int)raster->field_1E << 20) | ((unsigned int)t2 << 4));
            skyCamPutAD(data, 5);
            if ((flags & 1) != 0) {
                col = color[3];
                f0 = (float)col;
                f0 = f0 * D_004E0724;
                a = ((unsigned int)(int)f0 << 24) | ((unsigned int)color[2] << 16) | ((unsigned int)color[1] << 8) |
                    (unsigned int)color[0];
                data = a;
            } else {
                data = 0;
            }
            skyCamPutAD(data, 1);
            t0 = camera->field_60;
            offX = t0->field_1C;
            x2 = t2 + 0x20;
            right = t0->field_C + offX;
            if (right < x2) {
                x2 = right;
            }
            data = (unsigned long long)(long long)(int)(((unsigned int)(t0->field_10 + (int)t0->field_1E) << 20) | ((unsigned int)x2 << 4));
            skyCamPutAD(data, 5);
            raster = t0;
            t2 = (t2 + 0x20) & -0x20;
        } while (t2 < right);
    }
    skyCamPutAD(skyFrame_1, 0x4C);
    skyCamPutAD(skyZbuf_1, 0x4E);
    xy = skyXyoffset_1 & 0xFFFFFFF7FFFFFFFFULL;
    if (_rwDMAAsyncPktPtr[0] != 0) {
        xy = xy | skyCamIsrBits();
    }
    skyCamPutAD(xy, 0x18);
    skyCamPutAD(xy, 0x19);
    return 1;
}

// [C] the view-volume constants of the VU1 (called by SkyCameraBeginUpdate with the camera): with near = camera +0x80,
// far = +0x84, raster = camera +0x60 (w, h its width / height), parent = raster +0: vu1DataNearClip[0..3] = near,
// vu1DataFarClip[0..3] = far, vu1DataXYZScale = {(float)w, (float)h, camera +0x8C, 0}, vu1DataXYZShift = {0, 0, camera
// +0x90, 0}; with hw = (w + (w >> 31)) >> 1 (halves rounded toward zero), hh likewise, px = ((parent w + sign) >> 1) - 0x800,
// py likewise, ox = (short)offX - px, oy = (short)offY - py: vu1DataOffset2D = {(float)ox, (float)oy, 0, 0}, vu1DataOffset3D =
// {(float)(ox + hw), (float)(oy + hh), 0, 0}. xMaxYMax128[3] = far (or D_004E28B4 when
// D_004E28B0 is 0) and xMaxYMax128[2] = -255.0 / (that - camera +0x88). The clip vectors (skyClipVect1 / 2, skyCClipVect1 /
// 2: [0] = D_004E0718 - half video width / height (or + 5 for the C vectors), [1] = 1 / [0], [3] = far / near) and
// [2] depending on the projection (+0x14): perspective (1) uses far * near * -2 / (far - near) for vector 1, (far + near) /
// (far - near) for vector 2 and clears bit 3 of skyUploadedCode; any other projection uses 2 / (far - near) and
// -(far + near) / (far - near) and sets bit 3.
// 0x0020AEF0
extern "C" void _rwSkyPipeSetupForCamera(UnkStruct_skyCamera *camera)
{
    unsigned char *c = (unsigned char *)camera;
    UnkStruct_skyCamRaster *raster = camera->field_60;
    UnkStruct_skyCamRaster *parent;
    UnkStruct_skyCamVideo *vm;
    float f0, f1, f2, f3, f4, f5, f6, f7, f8, f9;
    int a1, a2, a0, v1, v0;

    f0 = skyCamF(c, 0x80);
    vu1DataNearClip[0] = f0;
    a1 = raster->field_C;
    vu1DataNearClip[1] = f0;
    vu1DataNearClip[3] = f0;
    f1 = (float)a1;
    vu1DataNearClip[2] = f0;
    a2 = raster->field_10;
    v0 = (int)((unsigned int)a1 >> 31);
    f0 = skyCamF(c, 0x84);
    f2 = (float)a2;
    vu1DataXYZScale[0] = f1;
    vu1DataFarClip[0] = f0;
    a1 = a1 + v0;
    vu1DataFarClip[1] = f0;
    v0 = (int)((unsigned int)a2 >> 31);
    vu1DataXYZScale[1] = f2;
    a2 = a2 + v0;
    vu1DataFarClip[3] = f0;
    a1 = a1 >> 1;
    vu1DataFarClip[2] = f0;
    parent = raster->field_0;
    a2 = a2 >> 1;
    f0 = skyCamF(c, 0x8C);
    vu1DataXYZScale[3] = 0.0f;
    vu1DataXYZScale[2] = f0;
    vu1DataXYZShift[0] = 0.0f;
    vu1DataXYZShift[1] = 0.0f;
    a0 = parent->field_C;
    f0 = skyCamF(c, 0x90);
    v1 = parent->field_10;
    v0 = (int)((unsigned int)a0 >> 31);
    a0 = a0 + v0;
    vu1DataXYZShift[2] = f0;
    vu1DataXYZShift[3] = 0.0f;
    v0 = (int)((unsigned int)v1 >> 31);
    v1 = v1 + v0;
    a0 = a0 >> 1;
    v0 = raster->field_1C;
    a0 = a0 - 0x800;
    v1 = v1 >> 1;
    v0 = v0 - a0;
    v1 = v1 - 0x800;
    a1 = v0 + a1;
    f1 = (float)v0;
    f0 = (float)a1;
    vu1DataOffset3D[0] = f0;
    v0 = raster->field_1E;
    vu1DataOffset2D[0] = f1;
    v0 = v0 - v1;
    vu1DataOffset3D[3] = 0.0f;
    a2 = v0 + a2;
    f1 = (float)v0;
    f0 = (float)a2;
    vu1DataOffset3D[2] = 0.0f;
    vu1DataOffset3D[1] = f0;
    vu1DataOffset2D[1] = f1;
    vu1DataOffset2D[3] = 0.0f;
    vu1DataOffset2D[2] = 0.0f;
    if (D_004E28B0 != 0) {
        f1 = skyCamF(c, 0x84);
        f2 = -255.0f;
        xMaxYMax128[3] = f1;
        f0 = skyCamF(c, 0x84);
        f1 = skyCamF(c, 0x88);
        f0 = f0 - f1;
        f2 = f2 / f0;
    } else {
        f1 = D_004E28B4;
        f2 = -255.0f;
        xMaxYMax128[3] = f1;
        f0 = skyCamF(c, 0x88);
        f1 = f1 - f0;
        f2 = f2 / f1;
    }
    xMaxYMax128[2] = f2;
    vm = skyVideoMode;
    f3 = D_004E0718;
    v0 = vm->field_0 >> 1;
    v1 = vm->field_4 >> 1;
    f1 = 1.0f;
    f0 = (float)v0;
    f4 = (float)v1;
    f5 = skyCamF(c, 0x80);
    f2 = skyCamF(c, 0x84);
    v0 = v0 + 5;
    f0 = f3 - f0;
    v1 = v1 + 5;
    f3 = f3 - f4;
    f8 = (float)v0;
    f4 = f1 / f0;
    skyClipVect1[0] = f0;
    f7 = f1 / f3;
    f9 = f1 / f8;
    f6 = (float)v1;
    a1 = camera->field_14;
    f2 = f2 - f5;
    skyClipVect1[1] = f4;
    f5 = f1 / f6;
    f4 = f1 / f2;
    f0 = skyCamF(c, 0x84);
    skyClipVect2[0] = f3;
    skyClipVect1[3] = f0;
    skyClipVect2[1] = f7;
    f0 = skyCamF(c, 0x80);
    skyCClipVect1[0] = f8;
    skyClipVect2[3] = f0;
    skyCClipVect1[1] = f9;
    f0 = skyCamF(c, 0x84);
    skyCClipVect2[0] = f6;
    skyCClipVect1[3] = f0;
    skyCClipVect2[1] = f5;
    f0 = skyCamF(c, 0x80);
    skyCClipVect2[3] = f0;
    if (a1 == 1) {
        f1 = skyCamF(c, 0x80);
        f2 = -2.0f;
        f0 = skyCamF(c, 0x84);
        f1 = f1 * f2;
        v0 = skyUploadedCode & 0xF7;
        f0 = f0 * f1;
        f0 = f0 * f4;
        skyClipVect1[2] = f0;
        skyCClipVect1[2] = f0;
        f1 = skyCamF(c, 0x80);
        f0 = skyCamF(c, 0x84);
        skyUploadedCode = (unsigned char)v0;
        f0 = f0 + f1;
        f0 = f0 * f4;
        skyClipVect2[2] = f0;
        skyCClipVect2[2] = f0;
        return;
    }
    f2 = skyCamF(c, 0x80);
    f1 = f4 + f4;
    f0 = skyCamF(c, 0x84);
    f3 = -f4;
    v0 = skyUploadedCode;
    f0 = f0 + f2;
    skyClipVect2[2] = f1;
    skyCClipVect2[2] = f1;
    v0 = v0 | 8;
    skyUploadedCode = (unsigned char)v0;
    f0 = f0 * f3;
    skyClipVect1[2] = f0;
    skyCClipVect1[2] = f0;
}
