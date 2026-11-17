// RenderWare 3.6 sky2 driver: instancing pipelines (RW 3.1 file name: driver/sky2/skyinst.c; reading aid only).
// Written from the retail asm (decomp assembly/asm/rw/renderware.s). Tags: [C] confirmed from the asm, [L], [G].
// boot1 staging (rw_wip).

// [C] an empty function (jr ra; nop).
// 0x0020B238
extern "C" void _rwSkyUnOverloadPipelines(void)
{
}

// [C] a qword of the VIF / DMA command templates initVu1DispatchStuff builds (pcpyld + sq; words in memory order).
struct UnkStruct_skyInstQword {
    unsigned int field_0;
    unsigned int field_4;
    unsigned int field_8;
    unsigned int field_C;
};
// [C] a qword of four floats (xMaxYMax128, colScale*Cmd128)
struct UnkStruct_skyInstVec {
    float field_0;
    float field_4;
    float field_8;
    float field_C;
};
// [C] nullLightBlock: 0x20 bytes (symbol table)
struct UnkStruct_nullLightBlock {
    UnkStruct_skyInstQword field_0;
    UnkStruct_skyInstQword field_10;
};
// [C] the first 0x38 bytes of ourGlobals ([L] RwGlobals; +0x28..+0x34 [L] dOpenDevice's Im2D render functions)
struct UnkStruct_ourGlobals_skyinst {
    unsigned char pad_0[0x28];
    void (*field_28)(void);     // [C] func_0020B240 ([L] fpIm2DRenderLine)
    void (*field_2C)(void);     // [C] func_0020B298 ([L] fpIm2DRenderTriangle)
    void (*field_30)(void);     // [C] func_0020B2F8 ([L] fpIm2DRenderPrimitive)
    void (*field_34)(void);     // [C] func_0020B368 ([L] fpIm2DRenderIndexedPrimitive)
};
// [C] customMode's first two words, the view initVu1DispatchStuff reads through skyVideoMode
struct UnkStruct_skyVideoMode_skyinst {
    int field_0;                // [C] draw width
    int field_4;                // [C] draw height
};

extern UnkStruct_ourGlobals_skyinst ourGlobals;         // [C] 0x00670BD0
extern UnkStruct_skyVideoMode_skyinst *skyVideoMode;    // [C] gp 0x004E1808 (&customMode after SkyStart)
extern UnkStruct_skyInstQword prologHeader3D128;        // [C] 0x0049AFD0
extern UnkStruct_skyInstQword prologHeader2D128;        // [C] 0x0049AFC0
extern UnkStruct_skyInstQword gifTag128;                // [C] 0x0049AE50
extern UnkStruct_skyInstQword dma1End128;               // [C] 0x0049AE20
extern UnkStruct_skyInstQword dma1Ret128;               // [C] 0x0049AE40
extern UnkStruct_skyInstQword dma1Header128;            // [C] 0x0049AE30
extern UnkStruct_skyInstQword dma10Header128;           // [C] 0x0049AE10
extern UnkStruct_skyInstQword runCmd128;                // [C] 0x0049AEA0
extern UnkStruct_skyInstQword contCmd128;               // [C] 0x0049AE00
extern UnkStruct_skyInstQword flushCmd128;              // [C] 0x0049AF80
extern UnkStruct_skyInstQword dmaEnd128;                // [C] 0x0049AF90
extern UnkStruct_skyInstQword baseOffsetCmd128;         // [C] 0x0049AF70
extern UnkStruct_skyInstQword zero128;                  // [C] 0x0049AEC0
extern UnkStruct_skyInstQword maskCmd128;               // [C] 0x0049AE70
extern UnkStruct_skyInstQword unmaskCmd128;             // [C] 0x0049AEB0
extern UnkStruct_skyInstVec xMaxYMax128;                // [C] 0x0049AF30 (read by game CB3CameraData::Construct)
extern UnkStruct_skyInstVec colScaleTexCmd128;          // [C] 0x0049AFB0
extern UnkStruct_skyInstVec colScaleNoTexCmd128;        // [C] 0x0049AFA0
extern UnkStruct_nullLightBlock nullLightBlock;         // [C] 0x0049AE80
extern unsigned int maxCopyTriPacketSize;               // [C] gp 0x004E18E8
extern unsigned int D_004E18EC;                         // [C] gp 0x004E18EC (_rwDMAInPkt.81 in the table)
extern float D_004E0718;                                // [C] 2047.9374 (the GS coordinate limit)
extern float D_004E071C;                                // [C] 0.502
extern float D_004E0720;                                // [C] 1.0000392
extern "C" void SyncDCache(void *start, void *end);

// [C] the Im2D render functions the driver installs: only their addresses are stored here (not ported)
extern "C" void func_0020B240(void);
extern "C" void func_0020B298(void);
extern "C" void func_0020B2F8(void);
extern "C" void func_0020B368(void);

// [C] n counted up from 0 while 3 * ((n + 16) >> 4) + 0x20 + 12 * (n + 1) < 0x1FF (unsigned), the global holding
// each n + 1 tried; it ends holding the first n that fails (39) [C 0x208C18..0x208C50 / 0x208C54..0x208CEC].
static void skyInstPacketLimit(unsigned int *g)
{
    unsigned int n;
    unsigned int t;

    *g = 0;
    do {
        n = *g;
        t = (n + 0x10) >> 4;
        *g = n + 1;
    } while ((t * 3 + 0x20 + (n + 1) * 0xC) < 0x1FF);
    *g = n;
}

// [C] the VU1 dispatch templates (qwords built with pcpyld, stored in the asm's order), xMaxYMax128 from the draw
// size (W / (K - (W >> 1)), H / (K - (H >> 1)), K = D_004E0718; words 2 and 3 untouched), the colour scales, the two
// packet limits and nullLightBlock, which is synced over 0xA0 bytes. No defined result.
// 0x002089B0
extern "C" void initVu1DispatchStuff(void)
{
    int w;
    int h;
    float k;
    float fw;
    float fh;
    float s;
    float one;

    k = D_004E0718;
    w = skyVideoMode->field_0;
    h = skyVideoMode->field_4;
    fh = k - (float)(h >> 1);
    fw = k - (float)(w >> 1);
    fw = (float)w / fw;
    fh = (float)h / fh;

    prologHeader3D128.field_0 = 0x1000000D;     // DMAtag cnt QWC 13; UNPACK V4-32 13 qwords to 0x3F0
    prologHeader3D128.field_4 = 0;
    prologHeader3D128.field_8 = 0;
    prologHeader3D128.field_C = 0x6C0D03F0;
    prologHeader2D128.field_0 = 0x10000003;     // DMAtag cnt QWC 3; UNPACK V4-32 3 qwords to 0x3F9
    prologHeader2D128.field_4 = 0;
    prologHeader2D128.field_8 = 0;
    prologHeader2D128.field_C = 0x6C0303F9;
    xMaxYMax128.field_0 = fw;
    zero128.field_0 = 0;
    zero128.field_4 = 0;
    zero128.field_8 = 0;
    zero128.field_C = 0;
    xMaxYMax128.field_4 = fh;
    s = D_004E071C;
    gifTag128.field_0 = 1;                      // GIFtag NLOOP 1, PRE, PACKED, NREG 3; ST, RGBAQ, XYZF2
    gifTag128.field_4 = 0x30004000;
    gifTag128.field_8 = 0x412;
    gifTag128.field_C = 0;
    one = D_004E0720;
    dma1End128.field_0 = 0x10000001;
    dma1End128.field_4 = 0;
    dma1End128.field_8 = 0;
    dma1End128.field_C = 0;
    dma1Ret128.field_0 = 0x60000001;
    dma1Ret128.field_4 = 0;
    dma1Ret128.field_8 = 0;
    dma1Ret128.field_C = 0;
    dma1Header128.field_0 = 0x10000001;
    dma1Header128.field_4 = 0;
    dma1Header128.field_8 = 0;
    dma1Header128.field_C = 0;
    colScaleNoTexCmd128.field_C = s;
    dma10Header128.field_0 = 0x1000000A;
    dma10Header128.field_4 = 0;
    dma10Header128.field_8 = 0;
    dma10Header128.field_C = 0;
    colScaleTexCmd128.field_0 = s;
    runCmd128.field_0 = 0x11000000;             // FLUSH, MSCAL 0
    runCmd128.field_4 = 0x15000000;
    runCmd128.field_8 = 0;
    runCmd128.field_C = 0;
    colScaleTexCmd128.field_C = s;
    colScaleNoTexCmd128.field_0 = one;
    contCmd128.field_0 = 0x10000000;            // FLUSHE, MSCNT
    contCmd128.field_4 = 0x17000000;
    contCmd128.field_8 = 0;
    contCmd128.field_C = 0;
    flushCmd128.field_0 = 0;                    // FLUSH in word 2
    flushCmd128.field_4 = 0;
    flushCmd128.field_8 = 0x11000000;
    flushCmd128.field_C = 0;
    colScaleNoTexCmd128.field_8 = one;
    dmaEnd128.field_0 = 0x10000000;
    dmaEnd128.field_4 = 0;
    dmaEnd128.field_8 = 0;
    dmaEnd128.field_C = 0;
    colScaleTexCmd128.field_4 = s;
    colScaleTexCmd128.field_8 = s;
    baseOffsetCmd128.field_0 = 0x03000000;      // BASE 0, OFFSET 0x1E1
    baseOffsetCmd128.field_4 = 0x020001E1;
    baseOffsetCmd128.field_8 = 0;
    baseOffsetCmd128.field_C = 0;
    colScaleNoTexCmd128.field_4 = one;

    skyInstPacketLimit(&D_004E18EC);
    skyInstPacketLimit(&maxCopyTriPacketSize);

    maskCmd128.field_0 = 0x11000000;            // FLUSH x3
    maskCmd128.field_4 = 0x11000000;
    maskCmd128.field_8 = 0x11000000;
    maskCmd128.field_C = 0;
    nullLightBlock.field_10.field_0 = 0;
    unmaskCmd128.field_0 = 0x11000000;          // FLUSH
    unmaskCmd128.field_4 = 0;
    unmaskCmd128.field_8 = 0;
    unmaskCmd128.field_C = 0;
    nullLightBlock.field_0.field_0 = 0x10000001;    // DMAtag cnt QWC 1; STCYCL 4/4; UNPACK V4-32 1 qword to 0x3D4
    nullLightBlock.field_0.field_4 = 0;
    nullLightBlock.field_0.field_8 = 0x01000404;
    nullLightBlock.field_0.field_C = 0x6C0103D4;
    nullLightBlock.field_10.field_4 = 0;
    nullLightBlock.field_10.field_8 = 0;
    nullLightBlock.field_10.field_C = 0;
    SyncDCache(&nullLightBlock, (char *)&nullLightBlock + 0x9F);
}

// [C] the four Im2D render functions into ourGlobals + 0x28..+0x34 (+0x30 last, in the call's delay slot), then
// initVu1DispatchStuff. No defined result.
// 0x0020B1E8
extern "C" void _rwSkyOverloadPipelines(void)
{
    ourGlobals.field_34 = func_0020B368;
    ourGlobals.field_28 = func_0020B240;
    ourGlobals.field_2C = func_0020B298;
    ourGlobals.field_30 = func_0020B2F8;
    initVu1DispatchStuff();
}
