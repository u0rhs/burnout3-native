// RenderWare 3.6 sky2 driver: blits (RW 3.1 file name: driver/sky2/skyblit.c; reading aid only).
// Written from the retail asm (decomp assembly/asm/rw/renderware.s). Tags: [C] confirmed from the asm, [L], [G].
// boot1 staging (rw_wip).

extern void *blitRaster[];   // [C] gp-relative, 8 bytes (symbol table): two rasters

extern "C" int RwRasterDestroy(void *raster);

// [C] destroys and clears each of the two blit rasters that is set. No defined result (v0 is whatever the last call
// left; SkySystem's request 0x12 ignores it).
// 0x00205DA8
extern "C" void _rwSkyRasterInitiateStop(void)
{
    if (blitRaster[0] != 0) {
        RwRasterDestroy(blitRaster[0]);
        blitRaster[0] = 0;
    }
    if (blitRaster[1] != 0) {
        RwRasterDestroy(blitRaster[1]);
        blitRaster[1] = 0;
    }
}

extern void *skyCurRas;      // [C] gp 0x004E2890
extern int rasterNum;        // [C] gp 0x004E28A0

// [C] RW core (baraster.cpp): flags 0x80 ([L] rwRASTERDONTALLOCATE)
extern "C" void *RwRasterCreate(int width, int height, int depth, int flags);

// [C] skyCurRas cleared, then the two blit rasters (0 x 0, depth 0, flags 0x80); when the second fails the first is
// destroyed and cleared (blitRaster[1] keeps the 0). Returns 1 (rasterNum cleared) when both exist, else 0.
// 0x00205D38
extern "C" int _rwSkyRasterFinaliseStart(void)
{
    skyCurRas = 0;
    blitRaster[0] = RwRasterCreate(0, 0, 0, 0x80);
    if (blitRaster[0] == 0) {
        return 0;
    }
    blitRaster[1] = RwRasterCreate(0, 0, 0, 0x80);
    if (blitRaster[1] == 0) {
        RwRasterDestroy(blitRaster[0]);
        blitRaster[0] = 0;
        return 0;
    }
    rasterNum = 0;
    return 1;
}
