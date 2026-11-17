// Sony EE library libgraph (SDK 2.8.0 as retail links it: the .data id string at 0x00483F10 reads "PsIIlibgraph2800").
// Written from the retail asm (decomp assembly/asm/sce/libgraph.s, data asm/data/sce/libgraph.{data,rodata}.s).
// Tags: [C] confirmed from the asm, [L] likely, [G] guess. Every GS / DMA / VIF / GIF register access is a b3hw call
// in the asm's order and width; VU1 status / FBRST (`cfc2` / `ctc2` vi29 / vi28) are b3hw_cop2_ctrl_*. `sync` /
// `sync.p` are comments. Kernel system-call wrappers (SetGsCrt, GsPutIMR, RemoveIntcHandler, _GetGsDxDyOffset) and
// the libkernl / libc helpers are called by name.
// boot2 (2026-11-05).

#include "b3hw/hw.h"

// [C] gp.15 @ 0x00483F20 (0x10 bytes; .data {0x00020001, 0x00030001, 0, 0}): libgraph's internal state
// ([L] sceGsGParam: interlace, output mode, field/frame mode, GS revision, the VSync callback and its handler id).
struct UnkStruct_sceGsGParam {
    short field_0;      // [C] interlace mode as passed to sceGsResetGraph
    short field_2;      // [C] output mode (2 NTSC, 3 PAL, 0x50 DTV480P are the ones sceGsDefDispEnv knows)
    short field_4;      // [C] (ffmode != 0)
    short field_6;      // [C] GS_CSR bits 16-23 (CSR.REV)
    int field_8;        // [C] tested by sceGsResetGraph / sceGsSyncV; nonzero selects VSync2 ([L] sceGsVSCfunc)
    int field_C;        // [C] passed to RemoveIntcHandler(2, ...) ([L] handler id)
};
extern UnkStruct_sceGsGParam D_00483F20;     // [C] gp.15 (not a C identifier)
extern unsigned int D_00483F30[];            // [C] init_vif_regs.12 @ 0x00483F30: two VIF1 qwords (8 words)

// [C] display environment written by sceGsDefDispEnv / read by sceGsPutDispEnv (0x28 bytes, five 64-bit words;
// [L] sceGsDispEnv: pmode, smode2, dispfb, display, bgcolor).
struct UnkStruct_sceGsDispEnv {
    unsigned long long field_0;     // [C] -> PMODE (0x12000000)
    unsigned long long field_8;     // [C] -> SMODE2 (0x12000020)
    unsigned long long field_10;    // [C] -> DISPFB2 (0x12000090; DISPFB1 for GS rev 1)
    unsigned long long field_18;    // [C] -> DISPLAY2 (0x120000A0; DISPLAY1 for GS rev 1)
    unsigned long long field_20;    // [C] -> BGCOLOR (0x120000E0; 0x120000C0 for GS rev 1)
};

// [C] draw environment built by sceGsSetDefDrawEnv / 2 (0x80 bytes: 8 GS register (value, address) pairs, value at
// the even word; [L] sceGsDrawEnv1 / 2: frame, zbuf, xyoffset, scissor, prmodecont, colclamp, dthe, test).
struct UnkStruct_sceGsDrawEnv {
    unsigned long long field_0;     // [C] FRAME value
    unsigned long long field_8;     // [C] 0x4C / 0x4D
    unsigned long long field_10;    // [C] ZBUF value
    unsigned long long field_18;    // [C] 0x4E / 0x4F
    unsigned long long field_20;    // [C] XYOFFSET value
    unsigned long long field_28;    // [C] 0x18 / 0x19
    unsigned long long field_30;    // [C] SCISSOR value
    unsigned long long field_38;    // [C] 0x40 / 0x41
    unsigned long long field_40;    // [C] |= 1 (PRMODECONT)
    unsigned long long field_48;    // [C] 0x1A
    unsigned long long field_50;    // [C] |= 1 (COLCLAMP)
    unsigned long long field_58;    // [C] 0x46
    unsigned long long field_60;    // [C] bit 0 = (psm & 2) != 0 (DTHE)
    unsigned long long field_68;    // [C] 0x45
    unsigned long long field_70;    // [C] TEST value
    unsigned long long field_78;    // [C] 0x47 / 0x48
};

// [C] rodata strings
extern char D_004B16C0[];       // [C] "rom0:ROMVER"
extern char D_004B16D0[];       // [C] "sceGsDefDispEnv:Not support displaymode for 0x%x!!\n"
extern char D_004B1708[];       // [C] "sceGsPutDrawEnv: DMA Ch.2 does not terminate\r\n"
// [C] 0x004B1738: libgraph's string "sceGsSyncPath: DMA Ch.1 ..." [L: the bytes start "sceGsSyn"; the D1 message by
// its place], under the name the decomp's symbol table gives that address (a misattributed label, kept as the table
// has it).
extern char __RTTI__Q23std13bad_exception[];
extern char D_004B1768[];       // [C] "\t<D1_CHCR=%08x:"
extern char D_004B1778[];       // [C] "D1_TADR=%08x:"
extern char D_004B1788[];       // [C] "D1_MADR=%08x:"
extern char D_004B1798[];       // [C] "D1_QWC=%08x>\r\n"
extern char D_004B17A8[];       // [C] "\t<D2_CHCR=%08x:"
extern char D_004B17B8[];       // [C] "D2_TADR=%08x:"
extern char D_004B17C8[];       // [C] "D2_MADR=%08x:"
extern char D_004B17D8[];       // [C] "D2_QWC=%08x>\r\n"
extern char D_004B17E8[];       // [C] "\t<VIF1_STAT=%08x:"
extern char D_004B1800[];       // [C] "GIF_STAT=%08x>\r\n"
extern char D_004B1818[];       // [C] "sceGsSyncPath: DMA Ch.2 does not terminate\r\n"
extern char D_004B1848[];       // [C] "sceGsSyncPath: VIF1 does not terminate\r\n"
extern char D_004B1878[];       // [C] "sceGsSyncPath: VU1 does not terminate\r\n"
extern char D_004B18A0[];       // [C] "sceGsSyncPath: GIF does not terminate\r\n"

// Kernel system-call wrappers (native platform tier) [C syscall numbers from libkernl.s / libgraph.s]
extern "C" int SetGsCrt(int inter, int omode, int ffmode);                 // [C] 0x0010BA20, syscall 0x02
extern "C" unsigned long long GsPutIMR(unsigned long long imr);            // [C] 0x0010C160, syscall 0x71
extern "C" int RemoveIntcHandler(int cause, int id);                       // [C] 0x0010BB20, syscall 0x11
extern "C" int _GetGsDxDyOffset(int omode, int *dx, int *dy, int *dx2, int *dy2);   // [C] 0x00102628, syscall 0x80
// libkernl / libc
extern "C" int DisableIntc(int cause);          // [C] 0x0010C900 (DIntr, syscall 0x15, EIntr)
extern "C" int VSync(void);                     // [C] 0x0010C2B0 (src/libs/sce/libkernl.cpp)
extern "C" int VSync2(void);                    // [C] 0x0010C340
extern "C" int sceOpen(const char *name, int flags, int mode);
extern "C" int sceRead(int fd, void *buf, int nbyte);
extern "C" int sceClose(int fd);
extern "C" int atoi(const char *s);
extern "C" int scePrintf(const char *fmt, ...);

extern "C" UnkStruct_sceGsGParam *sceGsGetGParam(void);

// [C] mode 0: GS_CSR = 0x200 (reset), state {inter, omode} stored, GS_CSR read back for the revision (bits 16-23),
// GsPutIMR(0xFF00), ffmode != 0 stored; when field_8 is set (never in retail [L]: no linked writer) the VSync
// handler is removed; then SetGsCrt(inter & 1, omode & 0xFF, ffmode & 1) (a tail jump). mode 1: GS_CSR = 0x100
// (flush) only. mode 5: mode 0 without the CSR reset, the IMR write and the handler removal. Other modes: nothing.
// 0x00102380
extern "C" void sceGsResetGraph(short mode, short inter, short omode, short ffmode)
{
    UnkStruct_sceGsGParam *gp;
    unsigned long long csr;

    if (mode == 1) {
        b3hw_write64(0x12001000, 0x100);
        return;
    }
    if (mode == 0) {
        gp = sceGsGetGParam();
        b3hw_write64(0x12001000, 0x200);
        gp->field_0 = inter;
        csr = b3hw_read64(0x12001000);
        gp->field_2 = omode;
        gp->field_6 = (short)((csr >> 16) & 0xFF);
        GsPutIMR(0xFF00);
        gp->field_4 = (ffmode != 0);
        if (gp->field_8 != 0) {
            DisableIntc(2);
            RemoveIntcHandler(2, gp->field_C);
            gp->field_C = 0;
            gp->field_8 = 0;
        }
        SetGsCrt(inter & 1, omode & 0xFF, ffmode & 1);
        return;
    }
    if (mode == 5) {
        gp = sceGsGetGParam();
        csr = b3hw_read64(0x12001000);
        gp->field_4 = (ffmode != 0);
        gp->field_0 = inter;
        gp->field_2 = omode;
        gp->field_6 = (short)((csr >> 16) & 0xFF);
        SetGsCrt(inter & 1, omode & 0xFF, ffmode & 1);
    }
}

// [C] the state block's address.
// 0x00102510
extern "C" UnkStruct_sceGsGParam *sceGsGetGParam(void)
{
    return &D_00483F20;
}

// [C] VIF1_FBRST (0x10003C10) = 1, VIF1_ERR (0x10003C20) = 2, VU1 reset bit (FBRST vi28 | 0x200), the two VIF1 init
// qwords of init_vif_regs.12 into VIF1_FIFO (0x10005000), GIF_CTRL (0x10003000) = 1 (GIF reset).
// 0x00102520
extern "C" void sceGsResetPath(void)
{
    unsigned int fbrst;

    b3hw_write32(0x10003C10, 1);
    b3hw_write32(0x10003C20, 2);
    // sync
    fbrst = b3hw_cop2_ctrl_read(28);
    b3hw_cop2_ctrl_write(28, fbrst | 0x200);
    // sync.p
    b3hw_write128(0x10005000, ((unsigned long long)D_00483F30[1] << 32) | D_00483F30[0],
                  ((unsigned long long)D_00483F30[3] << 32) | D_00483F30[2]);
    b3hw_write128(0x10005000, ((unsigned long long)D_00483F30[5] << 32) | D_00483F30[4],
                  ((unsigned long long)D_00483F30[7] << 32) | D_00483F30[6]);
    b3hw_write32(0x10003000, 1);
}

// [C] reads "rom0:ROMVER" one byte at a time into a 0x100-byte stack buffer up to its NUL (at most 0x100 bytes) and
// returns atoi(of the 9 bytes before the NUL) > 20010608 (0x1315670); -1 when sceOpen fails.
// 0x00102588
extern "C" int checkModelVersion(void)
{
    char buf[0x100];
    char *p;
    unsigned int i;
    int fd;

    fd = sceOpen(D_004B16C0, 1, 0);   // [G] the caller sets no a2; 0 passed
    if (fd < 0) {
        return -1;
    }
    p = buf;
    for (i = 0; i < 0x100; i++) {
        sceRead(fd, p, 1);
        if (*p++ == 0) {
            break;
        }
    }
    sceClose(fd);
    return 20010608 < atoi(buf + (int)(i - 9));
}

// [C] fills a display environment for the current output mode (state field_2): pmode 0x66, smode2, dispfb from psm /
// width, display from the mode's offsets (NTSC 2 / PAL 3: magnification (w + 0x9FF) / w; DTV480P 0x50: fixed), the
// BIOS display offsets (_GetGsDxDyOffset) when the mode is neither NTSC nor PAL and checkModelVersion() says so;
// an unknown mode prints a message and leaves display unwritten; bgcolor 0.
// 0x00102638
extern "C" void sceGsDefDispEnv(UnkStruct_sceGsDispEnv *disp, short psm, short w, short h, short dx, short dy)
{
    UnkStruct_sceGsGParam *gp;
    int off[4];
    int magh;
    int dh;
    unsigned long long dispx;
    unsigned long long dispy;
    short omode;

    gp = sceGsGetGParam();
    if ((unsigned int)((unsigned short)gp->field_2 - 2) < 2) {
        off[3] = 0;
        off[2] = 0;
        off[1] = 0;
        off[0] = 0;
    } else if (checkModelVersion() != 0) {
        _GetGsDxDyOffset(gp->field_2, &off[0], &off[1], &off[2], &off[3]);
    } else {
        off[3] = 0;
        off[2] = 0;
        off[1] = 0;
        off[0] = 0;
    }
    omode = gp->field_2;
    disp->field_0 = 0x66;
    if (gp->field_0 == 0) {
        disp->field_8 = 2;
    } else if (gp->field_4 != 0) {
        disp->field_8 = 3;
    } else {
        disp->field_8 = 1;
    }
    disp->field_10 = ((unsigned long long)(psm & 0xF) << 15) | ((unsigned long long)(((w + 0x3F) >> 6) & 0x3F) << 9);

    if (omode == 2 || omode == 3) {
        // [C] div with break 7 on w == 0 (0x102758 / 0x1027BC / 0x10280C / 0x102890)
        magh = (w + 0x9FF) / w;
        if (gp->field_0 == 1) {
            if (omode == 2) {
                dispy = (unsigned long long)((dy + off[1] + 0x32) & 0xFFF) << 12;
                dispx = (dx * magh + off[0] + 0x27C) & 0xFFF;
            } else {
                dispy = (unsigned long long)((dy + off[1] + 0x48) & 0xFFF) << 12;
                dispx = (dx * magh + off[0] + 0x290) & 0xFFF;
            }
            if (gp->field_4 != 0) {
                dh = h * 2 - 1;
            } else {
                dh = h - 1;
            }
        } else {
            if (omode == 2) {
                dispy = (unsigned long long)((dy + off[1] + 0x19) & 0xFFF) << 12;
                dispx = (dx * magh + off[0] + 0x27C) & 0xFFF;
            } else {
                dispy = (unsigned long long)((dy + off[1] + 0x24) & 0xFFF) << 12;
                dispx = (dx * magh + off[0] + 0x290) & 0xFFF;
            }
            dh = h - 1;
        }
        // [C] 64-bit shifts of sign-extended 32-bit values (dsll / dsll32)
        disp->field_18 = ((unsigned long long)(long long)(magh - 1) << 23) |
                         ((unsigned long long)(long long)(magh * w - 1) << 32) |
                         ((unsigned long long)(long long)dh << 44) | dispx | dispy;
    } else if (omode == 0x50) {
        dispx = ((((0x2D0 - w) / 2) * 2 + off[0]) + (dx * 2 + 0xE8)) & 0xFFF;
        dispy = (unsigned long long)((dy + off[1] + 0x23) & 0xFFF) << 12;
        disp->field_18 = ((unsigned long long)(long long)(h - 1) << 44) |
                         ((unsigned long long)(long long)(w * 2 - 1) << 32) | dispx | 0x800000 | dispy;
    } else {
        scePrintf(D_004B16D0, (int)omode);
    }
    disp->field_20 = 0;
}

// [C] writes the display environment to the GS: PMODE, then for GS revision 1 (state field_6) DISPFB1 / DISPLAY1 /
// 0x120000C0, otherwise SMODE2 / DISPFB2 / DISPLAY2 / BGCOLOR.
// 0x001029B0
extern "C" void sceGsPutDispEnv(UnkStruct_sceGsDispEnv *disp)
{
    UnkStruct_sceGsGParam *gp;

    gp = sceGsGetGParam();
    if (gp->field_6 == 1) {
        b3hw_write64(0x12000000, disp->field_0);
        b3hw_write64(0x12000070, disp->field_10);
        b3hw_write64(0x12000080, disp->field_18);
        b3hw_write64(0x120000C0, disp->field_20);
        return;
    }
    b3hw_write64(0x12000000, disp->field_0);
    b3hw_write64(0x12000020, disp->field_8);
    b3hw_write64(0x12000090, disp->field_10);
    b3hw_write64(0x120000A0, disp->field_18);
    b3hw_write64(0x120000E0, disp->field_20);
}

// [C] the Z buffer's base (in 2 KB pages): (w + 63) / 64 times (psm & 2 ? (h + 63) / 64 : (h + 31) / 32), doubled
// unless the state is interlaced (field_0 == 1) in field mode (field_4 == 0) [C 0x102AF0: the 64-bit load of the
// state masked with 0x0000FFFF0000FFFF compared with 1]; as a short.
// 0x00102A70
extern "C" short sceGszbufaddr(short psm, short w, short h)
{
    UnkStruct_sceGsGParam *gp;
    int wb;
    int hb;

    gp = sceGsGetGParam();
    wb = (w + 0x3F) / 64;
    if (psm & 2) {
        hb = (h + 0x3F) / 64;
    } else {
        hb = (h + 0x1F) / 32;
    }
    if ((unsigned short)gp->field_0 == 1 && (unsigned short)gp->field_4 == 0) {
        return (short)(wb * hb);
    }
    return (short)(wb * hb * 2);
}

// [C] fills draw environment 1 (context 1 register addresses 0x4C / 0x4E / 0x18 / 0x40 / 0x47): frame from psm /
// (w + 63) / 64, zbuf at sceGszbufaddr (ZMSK set when ztest is 0), xyoffset centred at 2048, scissor (0, 0, w-1,
// h-1), prmodecont / colclamp bit 0 set, dthe bit 0 = psm & 2, test (ztest & 3) << 17 | 0x10000 (ZTE) or 0x30000.
// Returns 8 (the number of registers).
// 0x00102B38
extern "C" int sceGsSetDefDrawEnv(UnkStruct_sceGsDrawEnv *draw, short psm, short w, short h, short ztest, short zpsm)
{
    unsigned long long zbuf;
    unsigned long long prmodecont;
    unsigned long long colclamp;
    long long hx;
    long long wx;

    draw->field_8 = 0x4C;
    draw->field_0 = ((unsigned long long)(((w + 0x3F) >> 6) & 0x3F) << 16) | ((unsigned long long)(psm & 0xF) << 24);
    draw->field_18 = 0x4E;
    if (ztest == 0) {
        zbuf = (unsigned long long)(long long)sceGszbufaddr(psm, w, h) | ((unsigned long long)(zpsm & 0xF) << 24) |
               0x100000000ULL;
    } else {
        zbuf = (unsigned long long)(long long)sceGszbufaddr(psm, w, h) | ((unsigned long long)(zpsm & 0xF) << 24);
    }
    draw->field_10 = zbuf;
    hx = (short)(h >> 1);
    wx = (short)(w >> 1);
    prmodecont = draw->field_40;
    colclamp = draw->field_50;
    draw->field_28 = 0x18;
    draw->field_20 = ((unsigned long long)(0x800 - wx) << 4) | ((unsigned long long)(0x800 - hx) << 36);
    draw->field_38 = 0x40;
    draw->field_30 = ((unsigned long long)(long long)(w - 1) << 16) | ((unsigned long long)(long long)(h - 1) << 48);
    draw->field_48 = 0x1A;
    draw->field_40 = prmodecont | 1;
    draw->field_58 = 0x46;
    draw->field_50 = colclamp | 1;
    draw->field_68 = 0x45;
    if (psm & 2) {
        draw->field_60 = draw->field_60 | 1;
    } else {
        draw->field_60 = draw->field_60 & ~1ULL;
    }
    draw->field_78 = 0x47;
    if (ztest != 0) {
        draw->field_70 = ((unsigned long long)(ztest & 3) << 17) | 0x10000;
    } else {
        draw->field_70 = 0x30000;
    }
    // sync
    return 8;
}

// [C] waits (counter up to 0x1000000) for DMA channel 2 (GIF, D2_CHCR 0x1000A000 bit 8) to stop, then sends the
// packet: D2_QWC = (giftag NLOOP & 0x7FFF) + 1, D2_MADR = the address (scratchpad: & 0x0FFFFFFF | 0x80000000;
// otherwise & 0x0FFFFFFF), D2_CHCR = 0x101. Returns 0; -1 (after a message) on the timeout.
// 0x00102D20
extern "C" int sceGsPutDrawEnv(void *packet)
{
    unsigned int i;
    unsigned int addr;

    if (b3hw_read32(0x1000A000) & 0x100) {
        i = 0;
        do {
            if (i > 0x1000000) {
                scePrintf(D_004B1708);
                return -1;
            }
            i++;
        } while (b3hw_read32(0x1000A000) & 0x100);
    }
    b3hw_write32(0x1000A020, (int)(*(unsigned long long *)packet & 0x7FFF) + 1);
    addr = (unsigned int)packet;
    if ((addr & 0x70000000) == 0x70000000) {
        b3hw_write32(0x1000A010, (addr & 0x0FFFFFFF) | 0x80000000);
    } else {
        b3hw_write32(0x1000A010, addr & 0x0FFFFFFF);
    }
    b3hw_write32(0x1000A000, 0x101);
    return 0;
}

// [C] waits for a vertical blank (VSync, or VSync2 when state field_8 is set); interlaced (field_0 == 1): returns
// the field, GS_CSR bit 13 (VSync2: bit 13 of its result); otherwise 1. The argument is not read.
// 0x00102E08
extern "C" int sceGsSyncV(int mode)
{
    UnkStruct_sceGsGParam *gp;
    int v;

    gp = sceGsGetGParam();
    if (gp->field_8 != 0) {
        v = VSync2();
        if (gp->field_0 != 1) {
            return 1;
        }
        return (v >> 13) & 1;
    }
    VSync();
    if (gp->field_0 != 1) {
        return 1;
    }
    return (int)((b3hw_read64(0x12001000) >> 13) & 1);
}

// [C] mode 0: waits for DMA ch.1 (D1_CHCR 0x10009000 bit 8), DMA ch.2 (D2_CHCR 0x1000A000 bit 8), VIF1 (VIF1_STAT
// 0x10003C00 & 0x1F000003), VU1 (vi29 bit 8) and the GIF (GIF_STAT 0x10003020 & 0xC00) in turn, with one counter
// shared by all five waits (timeout above 0x1000000: a message, the D1 / D2 / VIF1 / GIF registers printed, -1);
// returns 0. Other modes: returns the busy bits (1 D1, 2 D2, 4 VIF1, 8 VU1, 0x10 GIF) without waiting. The second
// argument is not read (a1 is cleared at entry, 0x102EA4).
// 0x00102EA0
extern "C" int sceGsSyncPath(int mode, unsigned short timeout)
{
    unsigned int i;
    char *msg;
    unsigned int v;
    int busy;

    i = 0;
    if (mode != 0) {
        busy = (b3hw_read32(0x10009000) & 0x100) != 0;
        if (b3hw_read32(0x1000A000) & 0x100) {
            busy |= 2;
        }
        if (b3hw_read32(0x10003C00) & 0x1F000003) {
            busy |= 4;
        }
        if (b3hw_cop2_ctrl_read(29) & 0x100) {
            busy |= 8;
        }
        if (b3hw_read32(0x10003020) & 0xC00) {
            busy |= 0x10;
        }
        return busy;
    }
    if (b3hw_read32(0x10009000) & 0x100) {
        do {
            if (i > 0x1000000) {
                msg = __RTTI__Q23std13bad_exception;
                goto timeout;
            }
            i++;
        } while (b3hw_read32(0x10009000) & 0x100);
    }
    if (b3hw_read32(0x1000A000) & 0x100) {
        do {
            if (i > 0x1000000) {
                msg = D_004B1818;
                goto timeout;
            }
            i++;
        } while (b3hw_read32(0x1000A000) & 0x100);
    }
    if (b3hw_read32(0x10003C00) & 0x1F000003) {
        do {
            if (i > 0x1000000) {
                msg = D_004B1848;
                goto timeout;
            }
            i++;
        } while (b3hw_read32(0x10003C00) & 0x1F000003);
    }
    if (b3hw_cop2_ctrl_read(29) & 0x100) {
        do {
            if (i > 0x1000000) {
                msg = D_004B1878;
                goto timeout;
            }
            i++;
        } while (b3hw_cop2_ctrl_read(29) & 0x100);
    }
    if (b3hw_read32(0x10003020) & 0xC00) {
        do {
            if (i > 0x1000000) {
                msg = D_004B18A0;
                goto timeout;
            }
            i++;
        } while (b3hw_read32(0x10003020) & 0xC00);
    }
    return 0;

timeout:
    scePrintf(msg);
    v = b3hw_read32(0x10009000);
    scePrintf(D_004B1768, v);
    v = b3hw_read32(0x10009030);
    scePrintf(D_004B1778, v);
    v = b3hw_read32(0x10009010);
    scePrintf(D_004B1788, v);
    v = b3hw_read32(0x10009020);
    scePrintf(D_004B1798, v);
    v = b3hw_read32(0x1000A000);
    scePrintf(D_004B17A8, v);
    v = b3hw_read32(0x1000A030);
    scePrintf(D_004B17B8, v);
    v = b3hw_read32(0x1000A010);
    scePrintf(D_004B17C8, v);
    v = b3hw_read32(0x1000A020);
    scePrintf(D_004B17D8, v);
    v = b3hw_read32(0x10003C00);
    scePrintf(D_004B17E8, v);
    v = b3hw_read32(0x10003020);
    scePrintf(D_004B1800, v);
    return -1;
}

// [C] sceGsSetDefDrawEnv for context 2 (register addresses 0x4D / 0x4F / 0x19 / 0x41 / 0x48), except that the frame
// width is w >> 6 (not rounded up: 0x1039A4 `sra 22` of w << 16). Returns 8.
// 0x00103988
extern "C" int sceGsSetDefDrawEnv2(UnkStruct_sceGsDrawEnv *draw, short psm, short w, short h, short ztest, short zpsm)
{
    unsigned long long zbuf;
    unsigned long long prmodecont;
    unsigned long long colclamp;
    long long hx;
    long long wx;

    draw->field_8 = 0x4D;
    draw->field_0 = ((unsigned long long)((w >> 6) & 0x3F) << 16) | ((unsigned long long)(psm & 0xF) << 24);
    draw->field_18 = 0x4F;
    if (ztest == 0) {
        zbuf = (unsigned long long)(long long)sceGszbufaddr(psm, w, h) | ((unsigned long long)(zpsm & 0xF) << 24) |
               0x100000000ULL;
    } else {
        zbuf = (unsigned long long)(long long)sceGszbufaddr(psm, w, h) | ((unsigned long long)(zpsm & 0xF) << 24);
    }
    draw->field_10 = zbuf;
    hx = (short)(h >> 1);
    wx = (short)(w >> 1);
    prmodecont = draw->field_40;
    colclamp = draw->field_50;
    draw->field_28 = 0x19;
    draw->field_20 = ((unsigned long long)(0x800 - wx) << 4) | ((unsigned long long)(0x800 - hx) << 36);
    draw->field_38 = 0x41;
    draw->field_30 = ((unsigned long long)(long long)(w - 1) << 16) | ((unsigned long long)(long long)(h - 1) << 48);
    draw->field_48 = 0x1A;
    draw->field_40 = prmodecont | 1;
    draw->field_58 = 0x46;
    draw->field_50 = colclamp | 1;
    draw->field_68 = 0x45;
    if (psm & 2) {
        draw->field_60 = draw->field_60 | 1;
    } else {
        draw->field_60 = draw->field_60 & ~1ULL;
    }
    draw->field_78 = 0x48;
    if (ztest != 0) {
        draw->field_70 = ((unsigned long long)(ztest & 3) << 17) | 0x10000;
    } else {
        draw->field_70 = 0x30000;
    }
    // sync
    return 8;
}

// [C] double-buffer swap ([L] sceGsDBuffDc): sceGsPutDispEnv(db + (id & 1) * 0x28), then sceGsPutDrawEnv of the
// buffer's draw packet (db + 0x1C0 for odd id, db + 0x50 for even); returns sceGsPutDrawEnv's result.
// 0x00103B68
extern "C" int sceGsSwapDBuffDc(void *db, int id)
{
    int odd;

    odd = id & 1;
    sceGsPutDispEnv((UnkStruct_sceGsDispEnv *)((char *)db + odd * 0x28));
    if (odd != 0) {
        return sceGsPutDrawEnv((char *)db + 0x1C0);
    }
    return sceGsPutDrawEnv((char *)db + 0x50);
}
