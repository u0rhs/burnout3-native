// rw/skyglobals.h -- sky2 driver globals that several src/libs/rw units share (boot2). One declaration per global, so
// every unit agrees on its type (the project's rule: a global declared with two types is an ODR trap once a combined or
// LTO build sees the units together). Guest words the asm treats as integers are RwUInt32 here; units cast at use.
#ifndef RW_SKYGLOBALS_H
#define RW_SKYGLOBALS_H

typedef unsigned int RwUInt32;   // [C RW 3.5 rwplcore.h: 32-bit]

// [C] gp 0x004E2860: the texture raster the render state names; a raster's guest address, 0, or 0xFFFFFFFF (the
// texture cache's "none valid" marker, compared at 0x20D2xx) -- a 32-bit guest word, not a host pointer.
extern RwUInt32 skyTextureRaster;

// [C] the packet cursor in the scratchpad (a guest address, 0 = no packet open); units cast to their qword struct at
// use ((T *)farFogPlane) and advance it in bytes.
extern RwUInt32 farFogPlane;
// [C] gp 0x004E2870 (symbol_addrs type:u32): the projection copy counter; SkyStart stores 0xDEADDEAD.
extern RwUInt32 skyProjState;
// [C] gp 0x004E27CC, a 4-byte symbol the code reads as bytes: [0] set by SkyStart, [1]..[3] flip state (badma.cpp).
extern unsigned char _rwDMAAsyncPktPtr[];
// [C] gp 0x004E1874 / 0x004E1870 (type:u32): raster pointers; units cast to their raster struct at use.
extern void *skyDrawRaster;
extern void *skyDisplayRaster;
// [C] gp 0x004E2874: one of the three fog tables (skyLinearFog / skyExpFog / skyExp2Fog).
extern void *fogTable;

#endif
