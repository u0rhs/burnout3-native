// Sony EE library libmpeg (SDK 2.8.0 as retail links it), the public API and set-up / reset paths. Written from the
// retail asm (decomp assembly/asm/sce/libmpeg.s; strings in asm/data/sce/libmpeg.rodata.s). Every hardware access is
// a b3hw_* call in the asm's order (include/b3hw/hw.h). Registers [C EE User's Manual 6.0 ch. 5, 8]: fromIPU (ch 3)
// CHCR 0x1000B000 / QWC 0x1000B020, toIPU (ch 4) CHCR 0x1000B400 / QWC 0x1000B420, toSPR (ch 9) CHCR 0x1000D400 /
// QWC 0x1000D420, D_ENABLER 0x1000F520 / D_ENABLEW 0x1000F590 (bit 16 suspends DMA), IPU_CTRL 0x10002010 (bit 30 RST,
// bit 23 MP1). Structures: sce/mpeg_internal.h. Tags: [C] confirmed from the asm, [L] likely, [G] guess.
// movie1 (2026-11-06).

#include "sce/mpeg_internal.h"
#include "b3hw/hw.h"

extern char D_004B1DF0[];   // [C] "The size of work area is too small"
extern char D_004B1E18[];   // [C] "work area size is too small"
extern char D_004B1F18[];   // [C] "[MPEG ERROR]%s\n"

extern "C" int func_00114E60(void);   // [C] DIntr: returns 1 when interrupts were enabled
extern "C" int EIntr(void);
extern "C" int scePrintf(const char *fmt, ...);
extern "C" int sprintf(char *buf, const char *fmt, ...);
extern "C" void *memset(void *p, int c, unsigned int n);
extern "C" void sceIpuInit(void);
extern "C" int sceIpuSync(int mode, unsigned short timeout);
extern "C" void sceIpuStopDMA(void *state);
extern "C" void sceIpuRestartDMA(void *state);

extern "C" int sceMpegReset(UnkStruct_sceMpeg *mp);
extern "C" int sceMpegClearRefBuff(UnkStruct_sceMpeg *mp);
extern "C" int _dispatchMpegCallback(UnkStruct_sceMpeg *mp, int *cbdata);
extern "C" void _alalcInit(UnkStruct_sceMpegAlalc *a, unsigned int base, unsigned int size);
extern "C" void _alalcSetDynamic(UnkStruct_sceMpegAlalc *a);
extern "C" unsigned int _alalcAlloc(UnkStruct_sceMpegData *d, UnkStruct_sceMpegAlalc *a, unsigned int size,
                                    unsigned int align);
extern "C" void _clearOnce(UnkStruct_sceMpegData *d);
extern "C" void _clearEach(UnkStruct_sceMpegData *d);
extern "C" void _initSeqAgain(UnkStruct_sceMpegData *d);
extern "C" void _ipuSetMPEG1(int on);
extern "C" void _Error(UnkStruct_sceMpegData *d, const char *msg);
extern "C" int func_0010B348(UnkStruct_sceMpeg *mp, void *cbdata, void *data);
extern "C" int _defRestartDMA(UnkStruct_sceMpeg *mp, void *cbdata, void *data);

// [C] IPU_CTRL.MP1 (bit 23) = on: read-modify-write of IPU_CTRL.
// 0x00106158
extern "C" void _ipuSetMPEG1(int on)
{
    b3hw_write32(0x10002010, (b3hw_read32(0x10002010) & 0xFF7FFFFF) | (on << 23));
}

// [C] with interrupts off and DMA suspended: fromIPU and toIPU CHCR.STR cleared (read-modify-write); then both
// channels' QWC = 0 and sceIpuInit. Returns 1. The game passes its CGtVideoDecoder (Construct); unused.
// 0x00109BF8
extern "C" int sceMpegInit(void)
{
    int was;

    was = func_00114E60();
    b3hw_write32(0x1000F590, b3hw_read32(0x1000F520) | 0x10000);
    b3hw_write32(0x1000B000, b3hw_read32(0x1000B000) & 0xFFFFFEFF);
    b3hw_write32(0x1000B400, b3hw_read32(0x1000B400) & 0xFFFFFEFF);
    b3hw_write32(0x1000F590, b3hw_read32(0x1000F520) & 0xFFFEFFFF);
    if (was) {
        EIntr();
    }
    b3hw_write32(0x1000B020, 0);
    b3hw_write32(0x1000B420, 0);
    sceIpuInit();
    return 1;
}

// [C] clears the work area, aligns its start up to 4 bytes for the internal state (an error and 0 when fewer
// than 0x10C0 bytes remain), puts the allocator over the rest (+0x10C0), sets the public fields (pts / dts -1), the
// callbacks (type 2 stop DMA = func_0010B348, type 3 restart = _defRestartDMA, the others none), a 0x800-byte block
// from the allocator (+0x60), the image records (+0x200 + 0x68 i), then _clearOnce, sceMpegReset, sceMpegClearRefBuff,
// the allocator's dynamic mark and the scratchpad / reference fields. Returns 1.
// 0x00109CA8
extern "C" int sceMpegCreate(UnkStruct_sceMpeg *mp, unsigned char *work, int size)
{
    UnkStruct_sceMpegData *d;
    unsigned int left;

    memset(work, 0, size);
    d = (UnkStruct_sceMpegData *)(((unsigned int)work + 3) >> 2 << 2);
    left = size - ((unsigned int)d - (unsigned int)work);
    if (left < 0x10C0) {
        _Error(d, D_004B1DF0);
        return 0;
    }
    mp->field_40 = d;
    _alalcInit(&d->field_120, (unsigned int)d + 0x10C0, left - 0x10C0);
    mp->field_0 = 0;
    mp->field_4 = 0;
    mp->field_8 = 0;
    mp->field_10 = -1;
    mp->field_18 = -1;
    mp->field_20 = 0;
    mp->field_28 = -1;
    mp->field_30 = -1;
    mp->field_38 = 0;
    d->field_CC[0] = 0;
    d->field_CC[1] = 0;
    d->field_CC[2] = 0;
    d->field_CC[3] = 0;
    d->field_CC[4] = 0;
    d->field_CC[5] = 0;
    d->field_CC[6] = 0;
    d->field_CC[7] = 0;
    d->field_CC[8] = 0;
    d->field_CC[9] = 0;
    d->field_CC[10] = 0;
    d->field_CC[11] = 0;
    d->field_CC[12] = 0;
    d->field_100 = 0;
    d->field_110 = 0;
    d->field_C[0].field_0 = 0;
    d->field_C[1].field_0 = 0;
    d->field_C[4].field_0 = 0;
    d->field_C[5].field_0 = 0;
    d->field_C[6].field_0 = 0;
    d->field_108 = -1;
    d->field_C[2].field_0 = (unsigned int)func_0010B348;
    d->field_C[3].field_0 = (unsigned int)_defRestartDMA;
    d->field_60 = _alalcAlloc(d, &d->field_120, 0x800, 8);
    d->field_64 = 0;
    d->field_114 = 0;
    d->field_118 = 0;
    d->field_11C = 0;
    d->field_8C = 0;
    d->field_90 = 0;
    d->field_98 = -1;
    d->field_A0 = 0;
    d->field_A8 = 0;
    d->field_C4 = 0;
    d->field_AC = -1;
    d->field_B0 = -1;
    d->field_B4 = -1;
    d->field_870 = mp;
    d->field_C8 = 1;
    _clearOnce(d);
    sceMpegReset(mp);
    sceMpegClearRefBuff(mp);
    d->field_1D0 = &d->field_200[0];
    d->field_1D4 = &d->field_200[1];
    d->field_1DC = &d->field_200[2];
    d->field_1E0 = &d->field_200[3];
    d->field_1E4 = &d->field_200[4];
    d->field_1EC = &d->field_200[5];
    d->field_1F0 = &d->field_200[6];
    d->field_1F4 = &d->field_200[7];
    d->field_1FC = &d->field_200[8];
    _alalcSetDynamic(&d->field_120);
    d->field_868 = -1;
    d->field_86C = 0;
    d->field_834 = 0x70003600;
    d->field_864 = 0;
    return 1;
}

// [C] returns 1.
// 0x00109EB8
extern "C" int sceMpegDelete(UnkStruct_sceMpeg *mp)
{
    return 1;
}

// [C] the internal state's end flag (+0).
// 0x0010A028
extern "C" int sceMpegIsEnd(UnkStruct_sceMpeg *mp)
{
    return mp->field_40->field_0;
}

// [C] clears the end / state words and the stop request, the frame count, +0xC4, +0x98 = -1, the IPU and its DMA
// (_clearEach), the picture count (+0x130), then _initSeqAgain. Returns 1.
// 0x0010A048
extern "C" int sceMpegReset(UnkStruct_sceMpeg *mp)
{
    UnkStruct_sceMpegData *d;

    d = mp->field_40;
    d->field_10A0 = 0;
    d->field_0 = 0;
    d->field_4 = 0;
    d->field_8 = 0;
    mp->field_8 = 0;
    d->field_C4 = 0;
    d->field_98 = -1;
    _clearEach(d);
    d->field_130 = 0;
    _initSeqAgain(d);
    return 1;
}

// [C] each reference record linked at +0x1D0, +0x1E0, +0x1F0, +0x1D4, +0x1E4, +0x1F4 (in that order) gets +0x28 = 0.
// Returns 1.
// 0x0010A0A8
extern "C" int sceMpegClearRefBuff(UnkStruct_sceMpeg *mp)
{
    UnkStruct_sceMpegData *d;

    d = mp->field_40;
    if (d->field_1D0 != 0) {
        d->field_1D0->field_28 = 0;
    }
    if (d->field_1E0 != 0) {
        d->field_1E0->field_28 = 0;
    }
    if (d->field_1F0 != 0) {
        d->field_1F0->field_28 = 0;
    }
    if (d->field_1D4 != 0) {
        d->field_1D4->field_28 = 0;
    }
    if (d->field_1E4 != 0) {
        d->field_1E4->field_28 = 0;
    }
    if (d->field_1F4 != 0) {
        d->field_1F4->field_28 = 0;
    }
    return 1;
}

// [C] slot `type` (12 bytes from +0xC) gets the function, its data and the caller's gp; returns the old function.
// 0x0010A100
extern "C" unsigned int sceMpegAddCallback(UnkStruct_sceMpeg *mp, int type, unsigned int func, void *data)
{
    UnkStruct_sceMpegCb *cb;
    unsigned int old;

    cb = &mp->field_40->field_C[type];
    old = cb->field_0;
    cb->field_0 = func;
    cb->field_4 = data;
    cb->field_8 = 0x004E8670;   // [C] $gp: crt0 sets it once to D_004E8670 (0x1001C0) and nothing changes it
    return old;
}

// [C] base and next-free = start, size, mark = start.
// 0x0010A280
extern "C" void _alalcInit(UnkStruct_sceMpegAlalc *a, unsigned int base, unsigned int size)
{
    a->field_C = base;
    a->field_4 = size;
    a->field_0 = base;
    a->field_8 = base;
}

// [C] mark = next free.
// 0x0010A298
extern "C" void _alalcSetDynamic(UnkStruct_sceMpegAlalc *a)
{
    a->field_C = a->field_8;
}

// [C] next free = mark.
// 0x0010A2A8
extern "C" void _alalcFree(UnkStruct_sceMpegAlalc *a)
{
    a->field_8 = a->field_C;
}

// [C] the next free address rounded up to align (unsigned division; align 0 traps with `break 7`), size bytes from
// there; past base + size: an error and 0, else next free moves past the block and the block is returned.
// 0x0010A2B8
extern "C" unsigned int _alalcAlloc(UnkStruct_sceMpegData *d, UnkStruct_sceMpegAlalc *a, unsigned int size,
                                    unsigned int align)
{
    unsigned int p;

    p = (a->field_8 + align - 1) / align * align;
    if (a->field_0 + a->field_4 < p + size) {
        _Error(d, D_004B1E18);
        return 0;
    }
    a->field_8 = p + size;
    return p;
}

// [C] +0x860 = 0, then MPEG-1 mode on in the IPU.
// 0x0010A840
extern "C" void _initSeqAgain(UnkStruct_sceMpegData *d)
{
    d->field_860 = 0;
    _ipuSetMPEG1(1);
}

// [C] MPEG-1 mode on; the scratchpad work addresses (+0x5A8 0x70000000, +0x5AC 0x70001800, +0x6E8 0x70001B00,
// +0x6EC 0x70003300); +0x828 = 0.
// 0x0010A8D0
extern "C" void _clearOnce(UnkStruct_sceMpegData *d)
{
    _ipuSetMPEG1(1);
    d->field_5A8 = 0x70000000;
    d->field_5AC = 0x70001800;
    d->field_6E8 = 0x70001B00;
    d->field_6EC = 0x70003300;
    d->field_828 = 0;
}

// [C] +0x830 = 1, +0x1C8 = 0; with interrupts off and DMA suspended: fromIPU, toIPU and toSPR CHCR = 0; then their
// QWC = 0, IPU reset (IPU_CTRL = RST) and sceIpuSync(0, 0).
// 0x0010A930
extern "C" void _clearEach(UnkStruct_sceMpegData *d)
{
    int was;

    d->field_830 = 1;
    d->field_1C8 = 0;
    was = func_00114E60();
    b3hw_write32(0x1000F590, b3hw_read32(0x1000F520) | 0x10000);
    b3hw_write32(0x1000B000, 0);
    b3hw_write32(0x1000B400, 0);
    b3hw_write32(0x1000D400, 0);
    b3hw_write32(0x1000F590, b3hw_read32(0x1000F520) & 0xFFFEFFFF);
    if (was) {
        EIntr();
    }
    b3hw_write32(0x1000B020, 0);
    b3hw_write32(0x1000B420, 0);
    b3hw_write32(0x1000D420, 0);
    b3hw_write32(0x10002010, 0x40000000);
    sceIpuSync(0, 0);
}

// [C] prints "[MPEG ERROR]%s\n" with the message.
// 0x0010A9F8
extern "C" void _ErrMessage(const char *msg)
{
    scePrintf(D_004B1F18, msg);
}

// [C] formats the message into a 0x100-byte stack buffer (sprintf with the caller's arguments) and reports it.
// 0x0010AA08
extern "C" void _Error1(UnkStruct_sceMpegData *d, const char *fmt, int a2, int a3)
{
    char buf[0x100];

    sprintf(buf, fmt, a2, a3);
    _Error(d, buf);
}

// [C] with a public object (+0x870), the state and an error callback (slot 0's function): the callback with
// {type 0, message}; otherwise the message is printed.
// 0x0010AA40
extern "C" void _Error(UnkStruct_sceMpegData *d, const char *msg)
{
    struct {
        int type;
        const char *msg;
    } cb;

    if (d->field_870 != 0 && d != 0 && d->field_C[0].field_0 != 0) {
        cb.msg = msg;
        cb.type = 0;
        _dispatchMpegCallback(d->field_870, (int *)&cb);
        return;
    }
    _ErrMessage(msg);
}

// [C] the default stop-DMA callback (slot 2): sceIpuStopDMA on the state's DMA record (+0x68). Returns 1.
// 0x0010B348
extern "C" int func_0010B348(UnkStruct_sceMpeg *mp, void *cbdata, void *data)
{
    sceIpuStopDMA(mp->field_40->field_68);
    return 1;
}

// [C] the default restart-DMA callback (slot 3): sceIpuRestartDMA on +0x68. Returns 1.
// 0x0010B370
extern "C" int _defRestartDMA(UnkStruct_sceMpeg *mp, void *cbdata, void *data)
{
    sceIpuRestartDMA(mp->field_40->field_68);
    return 1;
}
