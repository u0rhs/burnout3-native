// Sony EE library libmpeg (SDK 2.8.0 as retail links it): the IPU command layer -- sending commands, waiting for the
// IPU, the bit reader over FDEC (peek / flush / read n bits), start-code search, the callback dispatch. Written from
// the retail asm (decomp assembly/asm/sce/libmpeg.s); hardware through b3hw_* in the asm's order. IPU registers [C EE
// User's Manual 6.0 ch. 8.3]: IPU_CMD 0x10002000 (64-bit read: bit 63 busy), IPU_CTRL 0x10002010 (BUSY bit 31, ECD
// bit 14), IPU_BP 0x10002020. Structures: sce/mpeg_internal.h. Tags: [C] confirmed from the asm, [L], [G].
// movie1 (2026-11-06).
//
// The waits (_waitIpuIdle, _waitIpuIdle64 and the ones inlined in _peepBit / _flushBuf / _nextBit) poll IPU_CTRL /
// IPU_CMD while the IPU is busy without an error; every 5001 polls they call the "no data" callback (slot 1: the
// game's NodataCallback feeds the toIPU DMA); a stop request (+0x10A0) ends the wait: _waitIpuIdle / _waitIpuIdle64
// then stop both IPU DMA channels and reset the IPU.

#include "sce/mpeg_internal.h"
#include "b3hw/hw.h"

extern unsigned int _isDirty[];   // [C] 0x00483FD8: per command code (cmd >> 28), 1 = the command invalidates the
                                  // cached bits (+0x850 / +0x854): BCLR, IDEC, SETIQ, SETVQ, CSC, PACK, SETTH

extern "C" int func_00114E60(void);   // [C] DIntr: returns 1 when interrupts were enabled
extern "C" int EIntr(void);
extern "C" int _dispatchMpegCallback(UnkStruct_sceMpeg *mp, int *cbdata);
extern "C" int _dispatchMpegCbNodata(UnkStruct_sceMpeg *mp);
extern "C" unsigned long long _waitIpuIdle64(UnkStruct_sceMpegData *d);
extern "C" void _flushBuf(UnkStruct_sceMpegData *d, int n);
extern "C" unsigned int _peepBit(UnkStruct_sceMpegData *d, int n);
extern "C" void _waitIpuIdle(UnkStruct_sceMpegData *d);

// [C] the stop path of the waits: with interrupts off and DMA held, toIPU then fromIPU CHCR = 0; fromIPU QWC, toIPU
// QWC = 0; IPU reset.
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

// [C] the inlined wait of _peepBit / _flushBuf / _nextBit: while IPU_CTRL shows busy without an error and no stop is
// requested, poll, calling the no-data callback every 5001 polls.
static inline void mpeg_wait_busy(UnkStruct_sceMpegData *d)
{
    int n;

    if ((b3hw_read32(0x10002010) & 0x80004000) != 0x80000000) {
        return;
    }
    if (d->field_10A0 != 0) {
        return;
    }
    n = 0;
    do {
        if (n++ >= 0x1389) {
            _dispatchMpegCbNodata(d->field_870);
            n = 0;
        }
        if ((b3hw_read32(0x10002010) & 0x80004000) != 0x80000000) {
            return;
        }
    } while (d->field_10A0 == 0);
}

// [C] IPU_CMD = cmd; the cache's stale flag from the command code.
// 0x001073F8
extern "C" void _sendIpuCommand(UnkStruct_sceMpegData *d, unsigned int cmd)
{
    b3hw_write32(0x10002000, cmd);
    d->field_830 = _isDirty[cmd >> 28];
}

// [C] returns at once unless the IPU is busy without an error; else waits (no-data callback every 5001 polls) until
// it is not, or until a stop request (then the IPU DMA is stopped and the IPU reset).
// 0x00107428
extern "C" void _waitIpuIdle(UnkStruct_sceMpegData *d)
{
    int n;

    if ((b3hw_read32(0x10002010) & 0x80004000) != 0x80000000) {
        return;
    }
    n = 0;
    do {
        if (d->field_10A0 != 0) {
            mpeg_stop_ipu_dma();
            return;
        }
        if (n++ >= 0x1389) {
            _dispatchMpegCbNodata(d->field_870);
            n = 0;
        }
    } while ((b3hw_read32(0x10002010) & 0x80004000) == 0x80000000);
}

// [C] IPU_CMD read as 64 bits; while its busy bit (63) is set and IPU_CTRL shows no error: the same wait as
// _waitIpuIdle (stop request: DMA stopped, IPU reset). Returns the last IPU_CMD value read.
// 0x00107590
extern "C" unsigned long long _waitIpuIdle64(UnkStruct_sceMpegData *d)
{
    long long v;
    int n;

    v = (long long)b3hw_read64(0x10002000);
    if (v >= 0) {
        return v;
    }
    if (b3hw_read32(0x10002010) & 0x4000) {
        return v;
    }
    n = 0;
    for (;;) {
        if (d->field_10A0 != 0) {
            mpeg_stop_ipu_dma();
            return v;
        }
        if (n++ >= 0x1389) {
            _dispatchMpegCbNodata(d->field_870);
            n = 0;
        }
        v = (long long)b3hw_read64(0x10002000);
        if (v >= 0) {
            return v;
        }
        if (b3hw_read32(0x10002010) & 0x4000) {
            return v;
        }
    }
}

// [C] the next n (1..32) bits without consuming them: from the cache when it is current and holds n bits, else an
// FDEC 0 (after the inlined wait) fills it (+0x850 the 32 bits, +0x854 = 32).
// 0x00107988
extern "C" unsigned int _peepBit(UnkStruct_sceMpegData *d, int n)
{
    if (d->field_830 != 0 || d->field_854 < n) {
        mpeg_wait_busy(d);
        b3hw_write32(0x10002000, 0x40000000);
        d->field_830 = _isDirty[4];
        d->field_850 = (int)_waitIpuIdle64(d);
        d->field_854 = 32;
    }
    return (unsigned int)d->field_850 >> ((32 - n) & 31);
}

// [C] consumes n bits: (the inlined wait) FDEC n; the cache = the next 32 bits.
// 0x00107AA8
extern "C" void _flushBuf(UnkStruct_sceMpegData *d, int n)
{
    unsigned int cmd;

    mpeg_wait_busy(d);
    cmd = n | 0x40000000;
    b3hw_write32(0x10002000, cmd);
    d->field_830 = _isDirty[cmd >> 28];
    d->field_850 = (int)_waitIpuIdle64(d);
    d->field_854 = 32;
}

// [C] reads n bits: the cache refilled (FDEC 0) when stale or short, its top n bits are the result; then FDEC n
// consumes them and refills the cache.
// 0x00107BB8
extern "C" unsigned int _nextBit(UnkStruct_sceMpegData *d, int n)
{
    unsigned int cmd;
    unsigned int v;

    mpeg_wait_busy(d);
    if (d->field_830 != 0 || d->field_854 < n) {
        b3hw_write32(0x10002000, 0x40000000);
        d->field_830 = _isDirty[4];
        d->field_850 = (int)_waitIpuIdle64(d);
    }
    d->field_854 = 32;
    cmd = n | 0x40000000;
    v = (unsigned int)d->field_850 >> ((32 - n) & 31);
    b3hw_write32(0x10002000, cmd);
    d->field_830 = _isDirty[cmd >> 28];
    d->field_850 = (int)_waitIpuIdle64(d);
    return v;
}

// [C] waits for the IPU, aligns to a byte (flushes -BP & 7 bits), then flushes 8 bits at a time until the next 24 are
// 0x000001 or a stop is requested.
// 0x00107D28
extern "C" void _nextStartCode(UnkStruct_sceMpegData *d)
{
    int n;

    _waitIpuIdle(d);
    n = -(int)(b3hw_read32(0x10002020) & 7) & 7;
    if (n != 0) {
        _flushBuf(d, n);
    }
    while (_peepBit(d, 24) != 1) {
        if (d->field_10A0 != 0) {
            return;
        }
        _flushBuf(d, 8);
    }
}

// [C] calls the callback of slot cbdata[0] (the type), when the object, its state and the slot's function exist, with
// (mp, cbdata, the slot's data) under the gp it was registered with; returns its result, else 0.
// 0x0010A138
extern "C" int _dispatchMpegCallback(UnkStruct_sceMpeg *mp, int *cbdata)
{
    UnkStruct_sceMpegData *d;
    UnkStruct_sceMpegCb *cb;

    if (mp == 0) {
        return 0;
    }
    d = mp->field_40;
    if (d == 0) {
        return 0;
    }
    cb = &d->field_C[cbdata[0]];
    if (cb->field_0 == 0) {
        return 0;
    }
    return ((int (*)(UnkStruct_sceMpeg *, int *, void *))cb->field_0)(mp, cbdata, cb->field_4);
}

// [C] the no-data callback (type 1) with {1}; returns 1.
// 0x0010A1B0
extern "C" int _dispatchMpegCbNodata(UnkStruct_sceMpeg *mp)
{
    int cbdata[4];

    cbdata[0] = 1;
    _dispatchMpegCallback(mp, cbdata);
    return 1;
}
