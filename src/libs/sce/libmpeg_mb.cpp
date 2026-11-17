// Sony EE library libmpeg (SDK 2.8.0 as retail links it): the slice / macroblock / motion-vector layer -- picture
// data, slices, macroblock address increments, macroblock modes, skipped macroblocks, motion vectors (MPEG-1 and
// MPEG-2 forms, dual prime), VDEC through the IPU, waiting for a BDEC's output, and the header walk that reaches a
// picture (_nextHeader, _pictureHeader, _extensionAndUserData). Written from the retail asm (decomp
// assembly/asm/sce/libmpeg.s, 0x106180-0x108120); hardware through b3hw_* in the asm's order. IPU registers [C EE
// User's Manual 6.0 ch. 8.3]: IPU_CMD 0x10002000 (write: command; 64-bit read: result, bit 63 busy), IPU_CTRL
// 0x10002010 (BUSY bit 31, ECD bit 14, RST bit 30, PCT bits 24-26), IPU_BP 0x10002020, IPU_TOP 0x10002030 (64-bit:
// the next 32 bits, bit 63 busy). DMA: fromIPU (ch 3) CHCR 0x1000B000 / QWC 0x1000B020, toIPU (ch 4) CHCR 0x1000B400 /
// QWC 0x1000B420, toSPR CHCR 0x1000D400, D_ENABLER 0x1000F520 / D_ENABLEW 0x1000F590. Structures:
// sce/mpeg_internal.h. Tags: [C] confirmed from the asm, [L] likely, [G] guess. mpegA for movie2 (2026-11-07).
//
// The macroblock-level arrays the slice loop keeps on its stack (_slice0): pmv int[8] ([L] PMV[r][s][t]: r forward /
// backward 16 bytes apart, s first / second vector 8 bytes apart, t horizontal / vertical), mvfs int[4] ([L]
// motion_vertical_field_select[r][s] at r * 2 + s), dmv int[2] ([L] dmvector), and the macroblock's type, motion type
// and DCT type words.

#include "sce/mpeg_internal.h"
#include "b3hw/hw.h"

extern unsigned int _isDirty[];   // [C] 0x00483FD8: per command code (cmd >> 28), 1 = the command invalidates the
                                  // cached bits (+0x850 / +0x854)
extern unsigned int D_004B1CE0[]; // [C] the extension handlers by extension_start_code_identifier (0..10; larger ids
                                  // use entry 0): func_0010B318, _sequenceExtension, func_0010B278,
                                  // _quantMatrixExtension, _copyrightExtension, _sequenceScalableExtension,
                                  // func_0010B318, _pictureDisplayExtension, _pictureCodingExtension, func_0010B328,
                                  // func_0010B338

extern char D_004B1BB0[];   // [C] "Error code detected(BDEC)"
extern char D_004B1BD0[];   // [C] "Invalid macroblock_address_increment code(0x%08x)"
extern char D_004B1C08[];   // [C] "= Skip to the next picture ="
extern char D_004B1C28[];   // [C] "slice_start_code(0x%08x) out of range"
extern char D_004B1C50[];   // [C] "_sliceA0(): error happens"
extern char D_004B1C70[];   // [C] "Too many macroblocks in picture"
extern char D_004B1C90[];   // [C] "skiped macroblock in I picure is not allowed"
extern char D_004B1CC0[];   // [C] "Invalid macroblock_type code: 0"

extern "C" int func_00114E60(void);   // [C] DIntr: returns 1 when interrupts were enabled
extern "C" int EIntr(void);
extern "C" int _dispatchMpegCallback(UnkStruct_sceMpeg *mp, int *cbdata);
extern "C" int _dispatchMpegCbNodata(UnkStruct_sceMpeg *mp);
extern "C" void _waitIpuIdle(UnkStruct_sceMpegData *d);
extern "C" void _sendIpuCommand(UnkStruct_sceMpegData *d, unsigned int cmd);
extern "C" unsigned int _peepBit(UnkStruct_sceMpegData *d, int n);
extern "C" void _flushBuf(UnkStruct_sceMpegData *d, int n);
extern "C" unsigned int _nextBit(UnkStruct_sceMpegData *d, int n);
extern "C" void _nextStartCode(UnkStruct_sceMpegData *d);
extern "C" void _Error(UnkStruct_sceMpegData *d, const char *msg);
extern "C" void _Error1(UnkStruct_sceMpegData *d, const char *fmt, int a2, int a3);
extern "C" void receiveDataFromIPU(unsigned int addr, int size);
extern "C" void func_00105188(UnkStruct_sceMpegData *d, int buf);
extern "C" int _motionComp0(UnkStruct_sceMpegData *d, int mbaddr, int mbinc, int mbtype, int mtype, int *pmv,
                            int *mvfs, int *dmv);
extern "C" void func_0010ABB8(UnkStruct_sceMpegData *d);
extern "C" void _groupOfPicturesHeader(UnkStruct_sceMpegData *d);
extern "C" void _extrainfo(UnkStruct_sceMpegData *d);
extern "C" void _updateTempTackData(UnkStruct_sceMpegData *d, int tref);

extern "C" int _ipuVdec(UnkStruct_sceMpegData *d, int tbl);
extern "C" int _waitBdecOut(UnkStruct_sceMpegData *d);
extern "C" int _dmVector(UnkStruct_sceMpegData *d);
extern "C" void _dualPrimeVector(UnkStruct_sceMpegData *d, int *out, int *dmv, int mvx, int mvy);
extern "C" int _mbAddressIncrement(UnkStruct_sceMpegData *d);
extern "C" int _sliceA0(UnkStruct_sceMpegData *d, int n, int *mbaddr, int *mbinc, int *pmv);
extern "C" int _slice0(UnkStruct_sceMpegData *d, int n);
extern "C" int _skipMB0(UnkStruct_sceMpegData *d, int *pmv, int *mtype, int *mvfs, int *mbtype);
extern "C" int _decMB0(UnkStruct_sceMpegData *d, int *mbtype, int *mtype, int *dcttype, int *pmv, int *mvfs,
                       int *dmv);
extern "C" void _decode_motion_vector(int *pred, int rsize, int code, int residual, int fullpel);
extern "C" void _motionVectors(UnkStruct_sceMpegData *d, int *pmv, int *dmv, int *mvfs, int s, int count,
                               int frame, int hrsize, int vrsize, int dmvflag, int mvscale);
extern "C" void _motionVector(UnkStruct_sceMpegData *d, int *pmv, int *dmv, int hrsize, int vrsize, int dmvflag,
                              int mvscale, int fullpel);
extern "C" int _sliceB(UnkStruct_sceMpegData *d);
extern "C" int _nextHeader(UnkStruct_sceMpegData *d);
extern "C" void _pictureHeader(UnkStruct_sceMpegData *d);
extern "C" void _extensionAndUserData(UnkStruct_sceMpegData *d);

// [C] buffer i (0 / 1) of the two macroblock buffers from +0x5A8 (0x140 bytes each; sce/mpeg_internal.h).
static inline UnkStruct_sceMpegMbBuf *mpeg_mbbuf(UnkStruct_sceMpegData *d, int i)
{
    return (UnkStruct_sceMpegMbBuf *)&d->field_5A8 + i;
}

// [C] the stop path of the waits (the same sequence as libmpeg_bits.cpp's): with interrupts off and DMA held, toIPU
// then fromIPU CHCR = 0; fromIPU QWC, toIPU QWC = 0; IPU reset.
static inline void mpeg_stop_ipu_dma(void)
{
    int was;

    was = func_00114E60();
    b3hw_write32(0x1000F590, b3hw_read32(0x1000F520) | 0x10000);
    b3hw_write32(0x1000B400, 0);
    b3hw_write32(0x1000B000, 0);
    b3hw_write32(0x1000F590, b3hw_read32(0x1000F520) & 0xFFFEFFFF);
    if (was) {
        EIntr();
    }
    b3hw_write32(0x1000B020, 0);
    b3hw_write32(0x1000B420, 0);
    b3hw_write32(0x10002010, 0x40000000);
}

// [C] waits for the IPU after a BDEC: _waitIpuIdle, then while fromIPU still has QWC and IPU_CTRL shows no error: the
// no-data callback {1} whenever toIPU has no QWC and is not running, a stop request ends it (DMA stopped, IPU reset,
// 0). Then the bit cache from IPU_TOP / IPU_BP (+0x850 the 32 bits; +0x854 32 when TOP is not busy, else 32 - BP's
// bit position, 0 for position 0). An IPU error: "Error code detected(BDEC)", callbacks {2} (stop DMA), IPU reset,
// {3} (restart DMA), fromIPU stopped and its QWC cleared, 0. Returns 1 otherwise.
// 0x00106180
extern "C" int _waitBdecOut(UnkStruct_sceMpegData *d)
{
    int cb0[8];
    int cb1[8];
    int ret;
    long long top;
    unsigned int bp;
    unsigned int n;
    int was;

    ret = 1;
    _waitIpuIdle(d);
    if (b3hw_read32(0x1000B020) != 0 && (b3hw_read32(0x10002010) & 0x4000) == 0) {
        do {
            if (b3hw_read32(0x1000B420) == 0 && (b3hw_read32(0x1000B400) & 0x100) == 0) {
                cb0[0] = 1;
                _dispatchMpegCallback(d->field_870, cb0);
            }
            if (d->field_10A0 != 0) {
                mpeg_stop_ipu_dma();
                return 0;
            }
            if (b3hw_read32(0x1000B020) == 0) {
                break;
            }
        } while ((b3hw_read32(0x10002010) & 0x4000) == 0);
    }
    top = (long long)b3hw_read64(0x10002030);
    bp = b3hw_read32(0x10002020);
    d->field_850 = (int)top;
    if (top >= 0) {
        n = 32;
    } else if ((bp & 0x1F) != 0) {
        n = 32 - (bp & 0x1F);
    } else {
        n = 0;
    }
    d->field_854 = n;
    if (b3hw_read32(0x10002010) & 0x4000) {
        _Error(d, D_004B1BB0);
        cb1[0] = 2;
        _dispatchMpegCallback(d->field_870, cb1);
        b3hw_write32(0x10002010, 0x40000000);
        cb1[0] = 3;
        _dispatchMpegCallback(d->field_870, cb1);
        was = func_00114E60();
        b3hw_write32(0x1000F590, b3hw_read32(0x1000F520) | 0x10000);
        b3hw_write32(0x1000B000, 0);
        b3hw_write32(0x1000F590, b3hw_read32(0x1000F520) & 0xFFFEFFFF);
        if (was) {
            EIntr();
        }
        ret = 0;
        b3hw_write32(0x1000B020, 0);
    }
    return ret;
}

// [C] VDEC with table 3 ([L] dmvector).
// 0x00106418
extern "C" int _dmVector(UnkStruct_sceMpegData *d)
{
    return _ipuVdec(d, 3);
}

// [C] the dual-prime vectors (out[0..3]) from the vector (mvx, mvy) and dmv[0..1]; h(v) = (v + (v > 0)) >> 1 and
// t(v) = (3v + (v > 0)) >> 1 (arithmetic shifts). Frame picture (+0x18C == 3), +0x190 set: out = h(mvx) + dmv[0],
// h(mvy) + dmv[1] - 1, t(mvx) + dmv[0], t(mvy) + dmv[1] + 1; +0x190 clear: t(mvx) + dmv[0], t(mvy) + dmv[1] - 1,
// h(mvx) + dmv[0], h(mvy) + dmv[1] + 1. Field picture: out[0] = h(mvx) + dmv[0], out[1] = h(mvy) + dmv[1], then - 1
// for structure 1 ([L] top field), + 1 otherwise.
// 0x00106438
extern "C" void _dualPrimeVector(UnkStruct_sceMpegData *d, int *out, int *dmv, int mvx, int mvy)
{
    int v;

    if (d->field_18C == 3) {
        if (d->field_190 != 0) {
            out[0] = (mvx > 0 ? (mvx + 1) >> 1 : mvx >> 1) + dmv[0];
            out[1] = (mvy > 0 ? (mvy + 1) >> 1 : mvy >> 1) + dmv[1] - 1;
            v = mvx * 2 + mvx;
            if (mvx > 0) {
                v = v + 1;
            }
            out[2] = (v >> 1) + dmv[0];
            v = mvy * 2 + mvy;
            if (mvy > 0) {
                v = v + 1;
            }
            out[3] = (v >> 1) + dmv[1] + 1;
        } else {
            v = mvx * 2 + mvx;
            if (mvx > 0) {
                v = v + 1;
            }
            out[0] = (v >> 1) + dmv[0];
            v = mvy * 2 + mvy;
            if (mvy > 0) {
                v = v + 1;
            }
            out[1] = (v >> 1) + dmv[1] - 1;
            out[2] = (mvx > 0 ? (mvx + 1) >> 1 : mvx >> 1) + dmv[0];
            out[3] = (mvy > 0 ? (mvy + 1) >> 1 : mvy >> 1) + dmv[1] + 1;
        }
        return;
    }
    out[0] = (mvx > 0 ? (mvx + 1) >> 1 : mvx >> 1) + dmv[0];
    v = (mvy > 0 ? (mvy + 1) >> 1 : mvy >> 1) + dmv[1];
    out[1] = v;
    if (d->field_18C == 1) {
        out[1] = v - 1;
    } else {
        out[1] = v + 1;
    }
}

// [C] the macroblock address increment: VDEC table 0 until a code ends it. 0x22 ([L] stuffing) repeats; 0x23 ([L]
// escape) adds 33 and repeats; 0 (no code): with +0x860 set and the next 11 bits 15 they are flushed and it repeats,
// else "Invalid macroblock_address_increment code" (printed with 0), +0x134 = 1, returns 1; any other value is added
// and ends it. Returns the sum. The VDEC result is compared unsigned (sltiu).
// 0x001065C0
extern "C" int _mbAddressIncrement(UnkStruct_sceMpegData *d)
{
    int sum;
    int v;
    int more;
    unsigned int b;

    sum = 0;
    do {
        v = _ipuVdec(d, 0);
        if (v == 0x22) {
            more = 1;
        } else if ((unsigned int)v < 0x23) {
            if (v == 0) {
                b = _peepBit(d, 11);
                if (d->field_860 != 0 && b == 15) {
                    _flushBuf(d, 11);
                    more = 1;
                } else {
                    _Error1(d, D_004B1BD0, v, 0);   // [C] a2 = the VDEC result (0); a3 unset by retail (unused)
                    d->field_134 = 1;
                    return 1;
                }
            } else {
                sum += v;
                more = 0;
            }
        } else if (v != 0x23) {
            sum += v;
            more = 0;
        } else {
            sum += 0x21;
            more = 1;
        }
    } while (more);
    return sum;
}

// [C] decodes a picture's slices: +0x828 = +0x82C = 0; n = +0x144 * +0x148 macroblocks (halved, arithmetic, for a
// field picture: +0x18C != 3); _slice0 while it returns 1 or 3; then the IPU idle, the last BDEC's output
// (_waitBdecOut: a failure returns 4 on a stop request, else counts as result 2), the toSPR DMA's end (CHCR.STR
// polled); after result 0 the last macroblock is finished (func_00105188 on buffer +0x828 == 0); results 1 and 2
// report "= Skip to the next picture =". Returns 1 for result 0, else 0.
// 0x001066D0
extern "C" int _pictureData0(UnkStruct_sceMpegData *d)
{
    int n;
    int r;

    d->field_828 = 0;
    n = d->field_144 * d->field_148;
    d->field_82C = 0;
    if ((d->field_18C ^ 3) != 0) {
        n = n >> 1;
    }
    do {
        r = _slice0(d, n);
    } while (r == 1 || r == 3);
    _waitIpuIdle(d);
    if (_waitBdecOut(d) == 0) {
        if (d->field_10A0 != 0) {
            return 4;
        }
        r = 2;
    }
    while ((b3hw_read32(0x1000D400) >> 8) & 1) {
    }
    if (r == 0) {
        func_00105188(d, d->field_828 == 0);
    }
    if ((unsigned int)(r - 1) < 2) {
        _Error(d, D_004B1C08);
    }
    return r == 0;
}

// [C] a slice's start: +0x134 = 0, the next start code; one outside 0x101..0x1AF reports "slice_start_code(0x%08x)
// out of range" and returns 2. Else the code is consumed, the slice header (_sliceB: its result is the vertical
// position's extension) and the first address increment (*mbinc) read; an error there (+0x134) reports
// "_sliceA0(): error happens" and returns 1. Else *mbaddr = ((ext << 7) + (code & 0xFF) - 1) * +0x144 + inc - 1,
// *mbinc = 1, +0x1C8 = 1 (the DC predictors reset with the next BDEC), the PMVs cleared; returns 0.
// 0x001067E8
extern "C" int _sliceA0(UnkStruct_sceMpegData *d, int n, int *mbaddr, int *mbinc, int *pmv)
{
    unsigned int code;
    int ext;
    int inc;

    d->field_134 = 0;
    _nextStartCode(d);
    code = _peepBit(d, 32);
    if (code - 0x101 >= 0xAF) {
        _Error1(d, D_004B1C28, code, 0);   // [C] a2 = the code; a3 unset by retail (unused by the format)
        return 2;
    }
    _flushBuf(d, 32);
    ext = _sliceB(d);
    inc = _mbAddressIncrement(d);
    *mbinc = inc;
    if (d->field_134 != 0) {
        _Error(d, D_004B1C50);
        return 1;
    }
    *mbaddr = ((ext << 7) + (code & 0xFF) - 1) * d->field_144 + inc - 1;
    *mbinc = 1;
    d->field_1C8 = 1;
    pmv[2] = 0;
    pmv[5] = 0;
    pmv[4] = 0;
    pmv[1] = 0;
    pmv[0] = 0;
    pmv[7] = 0;
    pmv[6] = 0;
    pmv[3] = 0;
    return 0;
}

// [C] one slice: _sliceA0 (its nonzero result returned), then per macroblock while the address is below n: buffer
// +0x828's +0x13C = 0, the previous BDEC's output (_waitBdecOut: failure returns 4 on a stop request, else 2); when
// the increment is used up: the next 23 bits 0 ([L] the slice's end) or an error flag returns 3 (+0x134 cleared),
// else the next increment (an error: 1). Past n: "Too many macroblocks in picture", 2. Increment 1: _decMB0 (failure
// 1), else _skipMB0 (failure 2); then _motionComp0 (failure 2); from the second macroblock on the other buffer's
// macroblock is finished (func_00105188); address + 1, buffer flipped, increment - 1. Every failure exit clears
// +0x134. Returns 0 when the address reaches n.
// 0x00106918
extern "C" int _slice0(UnkStruct_sceMpegData *d, int n)
{
    int pmv[8];
    int mvfs[4];
    int dmv[4];
    int mbaddr;
    int mbinc;
    int mbtype;
    int mtype;
    int dcttype;
    int r;

    mbaddr = 0;
    mbinc = 0;
    r = _sliceA0(d, n, &mbaddr, &mbinc, pmv);
    if (r != 0) {
        return r;
    }
    d->field_134 = 0;
    for (;;) {
        if (!(mbaddr < n)) {
            return 0;
        }
        mpeg_mbbuf(d, d->field_828)->field_13C = 0;
        if (_waitBdecOut(d) == 0) {
            return d->field_10A0 != 0 ? 4 : 2;
        }
        if (mbinc == 0) {
            if (_peepBit(d, 23) == 0) {
                d->field_134 = 0;
                return 3;
            }
            if (d->field_134 != 0) {
                d->field_134 = 0;
                return 3;
            }
            mbinc = _mbAddressIncrement(d);
            if (d->field_134 != 0) {
                d->field_134 = 0;
                return 1;
            }
        }
        if (!(mbaddr < n)) {
            _Error(d, D_004B1C70);
            return 2;
        }
        if (mbinc == 1) {
            if (_decMB0(d, &mbtype, &mtype, &dcttype, pmv, mvfs, dmv) == 0) {
                d->field_134 = 0;
                return 1;
            }
        } else {
            if (_skipMB0(d, pmv, &mtype, mvfs, &mbtype) == 0) {
                d->field_134 = 0;
                return 2;
            }
        }
        if (_motionComp0(d, mbaddr, mbinc, mbtype, mtype, pmv, mvfs, dmv) == 0) {
            d->field_134 = 0;
            return 2;
        }
        if (mbaddr != 0) {
            func_00105188(d, d->field_828 ^ 1);
        }
        mbaddr = mbaddr + 1;
        d->field_828 = d->field_828 ^ 1;
        mbinc = mbinc - 1;
    }
}

// [C] a skipped macroblock: buffer +0x828's +0x13C = 1, +0x1C8 = 1; in a P picture (+0x168 == 2) the forward PMVs
// (pmv[0], [1], [4], [5]) cleared; frame picture: motion type 2, else 1 and mvfs[0] = mvfs[1] = (structure == 2); an
// I picture (+0x168 == 1) reports "skiped macroblock in I picure is not allowed" and returns 0, else 1; the
// macroblock type's intra bit (bit 0) is cleared either way.
// 0x00106B18
extern "C" int _skipMB0(UnkStruct_sceMpegData *d, int *pmv, int *mtype, int *mvfs, int *mbtype)
{
    int ok;
    int v;

    ok = 1;
    mpeg_mbbuf(d, d->field_828)->field_13C = 1;
    d->field_1C8 = 1;
    if (d->field_168 == 2) {
        pmv[0] = 0;
        pmv[5] = 0;
        pmv[4] = 0;
        pmv[1] = 0;
    }
    if (d->field_18C == 3) {
        *mtype = 2;
    } else {
        *mtype = 1;
        v = d->field_18C == 2;
        mvfs[0] = v;
        mvfs[1] = v;
    }
    if (d->field_168 == 1) {
        _Error(d, D_004B1C90);
        ok = 0;
    }
    *mbtype = *mbtype & ~1;
    return ok;
}

// [C] a coded macroblock: IPU_CTRL.PCT (bits 24-26) = +0x168; the macroblock type by VDEC table 1 (0: "Invalid
// macroblock_type code: 0", +0x134 = 1, 0). Motion type: forward / backward (bits 2-3) read 2 bits unless a frame
// picture with +0x194 (then 2); intra (bit 0) with +0x198: 2 in a frame picture, else 1. Vector count, format and
// scaling from it ([L] count 2 for field-in-frame / 16x8, format frame for frame motion, dmv for type 3, mvscale for
// field motion in a frame picture); the DCT type (1 bit) in a frame picture without +0x194 for intra / coded MBs,
// else 0; quantiser scale (bit 4: 5 bits into +0x1CC). Forward vectors (bit 3, or intra with +0x198): MPEG-2 (+0x860)
// _motionVectors with f_code +0x17C / +0x180 - 1, else _motionVector with +0x170 - 1 and full-pel +0x16C; backward
// (bit 2) likewise with +0x184 / +0x188 or +0x178 / +0x174; an error flag (+0x134) returns 0. A marker bit after
// concealment vectors. Intra or coded (bits 0-1): the BDEC output into buffer +0x828's +0x4 (receiveDataFromIPU
// 0x300), the IPU idle, BDEC (0x20000000 | DCR +0x1C8 << 26 | DT << 25 | MBI << 27 | QSC << 16); otherwise the
// buffer's +0x13C = 1. +0x1C8 = 0 (an error flag now returns 0); non-intra: +0x1C8 = 1; intra without concealment
// clears the PMVs. A P picture's macroblock without forward vector and not intra: forward PMVs cleared, motion type 2
// (frame picture) or 1 with mvfs[0] = (structure == 2). Returns 1.
// 0x00106BD8
extern "C" int _decMB0(UnkStruct_sceMpegData *d, int *mbtype, int *mtype, int *dcttype, int *pmv, int *mvfs,
                       int *dmv)
{
    int t;
    int ps;
    int mt;
    int count;
    int frame;
    int dmvflag;
    int mvscale;
    int v;

    b3hw_write32(0x10002010, (b3hw_read32(0x10002010) & 0xF8FFFFFF) | (d->field_168 << 24));
    t = _ipuVdec(d, 1);
    *mbtype = t;
    if (t == 0) {
        _Error(d, D_004B1CC0);
        d->field_134 = 1;
        return 0;
    }
    if (t & 0xC) {
        if (d->field_18C == 3 && d->field_194 != 0) {
            *mtype = 2;
        } else {
            *mtype = _nextBit(d, 2);
        }
    } else if (t & 1) {
        if (d->field_198 != 0) {
            *mtype = (d->field_18C == 3) ? 2 : 1;
        }
    }
    ps = d->field_18C;
    mt = *mtype;
    if (ps == 3) {
        count = (mt == 1) ? 2 : 1;
        frame = mt == 2;
    } else {
        count = (mt == 2) ? 2 : 1;
        frame = 0;
    }
    dmvflag = mt == 3;
    mvscale = 0;
    if (frame == 0) {
        mvscale = ps == 3;
    }
    if (ps == 3 && d->field_194 == 0 && (*mbtype & 3) != 0) {
        v = _nextBit(d, 1);
    } else {
        v = 0;
    }
    *dcttype = v;
    if (*mbtype & 0x10) {
        d->field_1CC = _nextBit(d, 5);
    }
    if ((*mbtype & 8) != 0 || ((*mbtype & 1) != 0 && d->field_198 != 0)) {
        if (d->field_860 != 0) {
            _motionVectors(d, pmv, dmv, mvfs, 0, count, frame, d->field_17C - 1, d->field_180 - 1, dmvflag,
                           mvscale);
        } else {
            _motionVector(d, pmv, dmv, d->field_170 - 1, d->field_170 - 1, 0, 0, d->field_16C);
        }
    }
    if (d->field_134 != 0) {
        return 0;
    }
    if (*mbtype & 4) {
        if (d->field_860 != 0) {
            _motionVectors(d, pmv, dmv, mvfs, 1, count, frame, d->field_184 - 1, d->field_188 - 1, 0, mvscale);
        } else {
            _motionVector(d, &pmv[2], dmv, d->field_178 - 1, d->field_178 - 1, 0, 0, d->field_174);
        }
    }
    if (d->field_134 != 0) {
        return 0;
    }
    if ((*mbtype & 1) != 0 && d->field_198 != 0) {
        _flushBuf(d, 1);
    }
    if (*mbtype & 3) {
        receiveDataFromIPU(mpeg_mbbuf(d, d->field_828)->field_4, 0x300);
        _waitIpuIdle(d);
        _sendIpuCommand(d, ((*mbtype & 1) << 27) | (d->field_1CC << 16) | ((d->field_1C8 << 26) | 0x20000000) |
                               (*dcttype << 25));
    } else {
        mpeg_mbbuf(d, d->field_828)->field_13C = 1;
    }
    v = d->field_134;
    d->field_1C8 = 0;
    if (v != 0) {
        return 0;
    }
    if ((*mbtype & 1) == 0) {
        d->field_1C8 = 1;
    }
    if ((*mbtype & 1) != 0 && d->field_198 == 0) {
        pmv[5] = 0;
        pmv[4] = 0;
        pmv[1] = 0;
        pmv[0] = 0;
        pmv[7] = 0;
        pmv[6] = 0;
        pmv[3] = 0;
        pmv[2] = 0;
    }
    if (d->field_168 != 2) {
        return 1;
    }
    if (*mbtype & 9) {
        return 1;
    }
    pmv[0] = 0;
    pmv[5] = 0;
    pmv[4] = 0;
    pmv[1] = 0;
    if (d->field_18C == 3) {
        *mtype = 2;   // [C] retail stores +0x168, which is 2 here (0x107028)
    } else {
        *mtype = 1;
        mvfs[0] = d->field_18C == 2;
    }
    return 1;
}

// [C] one vector component: the prediction (halved, arithmetic, when full-pel) plus the decoded difference, wrapped
// into [-(16 << r), (16 << r)): code > 0 adds 1 + ((code - 1) << r) + residual, code < 0 subtracts 1 + ((-code - 1)
// << r) + residual; stored back (doubled when full-pel).
// 0x00107080
extern "C" void _decode_motion_vector(int *pred, int rsize, int code, int residual, int fullpel)
{
    int lim;
    int v;

    v = *pred;
    lim = 16 << (rsize & 31);
    if (fullpel) {
        v = v >> 1;
    }
    if (code > 0) {
        v = v + 1;
        v = v + (((code - 1) << (rsize & 31)) + residual);
        if (!(v < lim)) {
            v = v - (lim << 1);
        }
    } else if (code < 0) {
        v = v - 1;
        v = v - ((~code << (rsize & 31)) + residual);
        if (v < -lim) {
            v = v + (lim << 1);
        }
    }
    *pred = fullpel ? v << 1 : v;
}

// [C] the MPEG-2 vectors of direction s: one vector (count 1) -- for field format without dmv a field select bit
// first (mvfs[s] and mvfs[s + 2]), then the vector into pmv[s * 2], copied to pmv[s * 2 + 4]; two vectors -- field
// select, vector, field select, vector (mvfs[s], pmv[s * 2]; mvfs[s + 2], pmv[s * 2 + 4]).
// 0x00107108
extern "C" void _motionVectors(UnkStruct_sceMpegData *d, int *pmv, int *dmv, int *mvfs, int s, int count,
                               int frame, int hrsize, int vrsize, int dmvflag, int mvscale)
{
    int v;

    if (count == 1) {
        if (frame == 0 && dmvflag == 0) {
            v = _nextBit(d, 1);
            mvfs[s + 2] = v;
            mvfs[s] = v;
        }
        _motionVector(d, &pmv[s * 2], dmv, hrsize, vrsize, dmvflag, mvscale, 0);
        pmv[s * 2 + 4] = pmv[s * 2];
        pmv[s * 2 + 5] = pmv[s * 2 + 1];
        return;
    }
    mvfs[s] = _nextBit(d, 1);
    _motionVector(d, &pmv[s * 2], dmv, hrsize, vrsize, dmvflag, mvscale, 0);
    mvfs[s + 2] = _nextBit(d, 1);
    _motionVector(d, &pmv[s * 2 + 4], dmv, hrsize, vrsize, dmvflag, mvscale, 0);
}

// [C] one vector: per component a motion code (VDEC table 2), its residual (hrsize / vrsize bits when both are
// nonzero, else 0), _decode_motion_vector; with dmvflag a dmvector after each (dmv[0], dmv[1]); with mvscale the
// vertical prediction is halved before and doubled after (field vector in a frame picture).
// 0x001072A8
extern "C" void _motionVector(UnkStruct_sceMpegData *d, int *pmv, int *dmv, int hrsize, int vrsize, int dmvflag,
                              int mvscale, int fullpel)
{
    int code;
    int res;

    code = _ipuVdec(d, 2);
    if (hrsize != 0 && code != 0) {
        res = _nextBit(d, hrsize);
    } else {
        res = 0;
    }
    _decode_motion_vector(pmv, hrsize, code, res, fullpel);
    if (dmvflag) {
        dmv[0] = _dmVector(d);
    }
    code = _ipuVdec(d, 2);
    if (vrsize != 0 && code != 0) {
        res = _nextBit(d, vrsize);
    } else {
        res = 0;
    }
    if (mvscale) {
        pmv[1] = pmv[1] >> 1;
    }
    _decode_motion_vector(&pmv[1], vrsize, code, res, fullpel);
    if (mvscale) {
        pmv[1] = pmv[1] << 1;
    }
    if (dmvflag) {
        dmv[1] = _dmVector(d);
    }
}

// [C] VDEC with table tbl (0x30000000 | tbl << 26) after the inlined busy wait (as _peepBit's: no-data callback
// every 5001 polls, a stop request ends the wait); the cache's stale flag from the command code. While the result
// (IPU_CMD, 64-bit) is busy: a stop request stops the IPU DMA, resets the IPU and returns 0; else the no-data callback
// every 5001 polls. Then the cache from IPU_TOP / IPU_BP (+0x850; +0x854 32 when TOP is not busy, else -BP & 31),
// +0x134 = (the result's low word == 0); returns the result's low 16 bits sign-extended.
// 0x00107718
extern "C" int _ipuVdec(UnkStruct_sceMpegData *d, int tbl)
{
    unsigned int cmd;
    long long v;
    long long top;
    unsigned int bp;
    int n;

    if ((b3hw_read32(0x10002010) & 0x80004000) == 0x80000000 && d->field_10A0 == 0) {
        n = 0;
        do {
            if (n++ >= 0x1389) {
                _dispatchMpegCbNodata(d->field_870);
                n = 0;
            }
            if ((b3hw_read32(0x10002010) & 0x80004000) != 0x80000000) {
                break;
            }
        } while (d->field_10A0 == 0);
    }
    cmd = (tbl << 26) | 0x30000000;
    b3hw_write32(0x10002000, cmd);
    v = (long long)b3hw_read64(0x10002000);
    d->field_830 = _isDirty[(int)cmd >> 28];
    if (v < 0) {
        n = 0;
        for (;;) {
            if (d->field_10A0 != 0) {
                mpeg_stop_ipu_dma();
                return 0;
            }
            if (n++ >= 0x1389) {
                n = 0;
                _dispatchMpegCbNodata(d->field_870);
            }
            v = (long long)b3hw_read64(0x10002000);
            if (v >= 0) {
                break;
            }
        }
    }
    top = (long long)b3hw_read64(0x10002030);
    bp = b3hw_read32(0x10002020);
    d->field_850 = (int)top;
    if (top >= 0) {
        d->field_854 = 32;
    } else {
        d->field_854 = -(int)(bp & 0x1F) & 0x1F;
    }
    d->field_134 = (int)v == 0;
    return (short)(v & 0xFFFF);
}

// [C] the slice header after the start code: quantiser_scale_code (5 bits into +0x1CC), then when the next bit is
// set: 1 bit ([L] intra_slice), 7 bits flushed ([L] reserved), _extrainfo. Returns 0 ([L] no vertical position
// extension).
// 0x00107DA8
extern "C" int _sliceB(UnkStruct_sceMpegData *d)
{
    d->field_1CC = _nextBit(d, 5);
    if (_nextBit(d, 1) != 0) {
        _nextBit(d, 1);
        _flushBuf(d, 7);
        _extrainfo(d);
    }
    return 0;
}

// [C] walks the start codes until a picture: a stop request returns -1; 0x1B3 sequence header (func_0010ABB8), 0x1B8
// group of pictures (_groupOfPicturesHeader), others skipped; 0x1B7 ([L] sequence end) returns 0; 0x100 picture:
// _pictureHeader, the type-5 callback with {5, -1, -1} (two 64-bit words at +8 / +0x10 it may fill: +0x840 / +0x848),
// returns the picture coding type (+0x168).
// 0x00107E10
extern "C" int _nextHeader(UnkStruct_sceMpegData *d)
{
    struct {
        int type;
        int pad;
        long long a;
        long long b;
    } cb;
    unsigned int code;

    for (;;) {
        if (d->field_10A0 != 0) {
            return -1;
        }
        _nextStartCode(d);
        code = _nextBit(d, 32);
        if (code == 0x1B3) {
            func_0010ABB8(d);
        } else if (code < 0x1B4) {
            if (code == 0x100) {
                _pictureHeader(d);
                cb.type = 5;
                cb.b = -1;
                cb.a = -1;
                _dispatchMpegCallback(d->field_870, (int *)&cb);
                d->field_848 = cb.b;
                d->field_840 = cb.a;
                return d->field_168;
            }
        } else if (code == 0x1B7) {
            return 0;
        } else if (code == 0x1B8) {
            _groupOfPicturesHeader(d);
        }
    }
}

// [C] the picture header: temporal reference (10 bits), picture_coding_type (3 bits, +0x168), 16 bits ([L]
// vbv_delay); P / B: full-pel (1 bit, +0x16C) and forward f_code (3 bits, +0x170); B: +0x174 / +0x178 likewise
// (backward); _extrainfo, _extensionAndUserData, then _updateTempTackData with the temporal reference.
// 0x00107F30
extern "C" void _pictureHeader(UnkStruct_sceMpegData *d)
{
    int tref;

    tref = _nextBit(d, 10);
    d->field_168 = _nextBit(d, 3);
    _nextBit(d, 16);
    if ((unsigned int)(d->field_168 - 2) < 2) {
        d->field_16C = _nextBit(d, 1);
        d->field_170 = _nextBit(d, 3);
    }
    if (d->field_168 == 3) {
        d->field_174 = _nextBit(d, 1);
        d->field_178 = _nextBit(d, 3);
    }
    _extrainfo(d);
    _extensionAndUserData(d);
    _updateTempTackData(d, tref);
}

// [C] the handler table (D_004B1CE0, 11 words) copied to the stack; the next start code; then while the next 32 bits
// are 0x1B5 (extension: consumed, its 4-bit id picks the handler, ids above 10 use entry 0, called with d) or 0x1B2
// (user data: consumed), the next start code after each.
// 0x00107FF8
extern "C" void _extensionAndUserData(UnkStruct_sceMpegData *d)
{
    unsigned int tab[11];
    unsigned int code;
    unsigned int id;
    int i;

    for (i = 0; i < 11; i++) {
        tab[i] = D_004B1CE0[i];
    }
    _nextStartCode(d);
    for (;;) {
        code = _peepBit(d, 32);
        if (code == 0x1B5) {
            _flushBuf(d, 32);
            id = _nextBit(d, 4);
            if (10 < id) {
                id = 0;
            }
            ((void (*)(UnkStruct_sceMpegData *))tab[id])(d);
            _nextStartCode(d);
        } else if (code == 0x1B2) {
            _flushBuf(d, 32);
            _nextStartCode(d);
        } else {
            return;
        }
    }
}
