// Sony EE library libmpeg (SDK 2.8.0 as retail links it): the header / extension parsers (sequence, GOP, picture
// coding, quant matrix, display, copyright), picture / frame / field decode and skip, _getpic and the
// sceMpegGetPicture* API, the flush at the stream's end and the small accessors. Written from the retail asm (decomp
// assembly/asm/sce/libmpeg.s; strings in asm/data/sce/libmpeg.rodata.s, the extension table D_004B1CE0 calls the
// extension parsers here by id). Hardware through b3hw_* in the asm's order. IPU registers [C EE User's Manual 6.0
// ch. 8.3]: IPU_CMD 0x10002000, IPU_CTRL 0x10002010 (IDP bits 16-17, AS bit 20, IVF bit 21, QST bit 22). toIPU DMA
// (ch 4): CHCR 0x1000B400, MADR 0x1000B410, QWC 0x1000B420. Structures: sce/mpeg_internal.h. Tags: [C] confirmed from
// the asm, [L] likely, [G] guess. mpegB (movie2, 2026-11-07).
//
// UnkStruct_sceMpegData's field_CC[] words: [0..5] +0xCC..+0xE0 the display offsets / sizes (_dispRefImage),
// [6] +0xE4, [7] +0xE8, [8] +0xEC the first picture structure seen, [9] +0xF0 the output buffer, [10] +0xF4 /
// [11] +0xF8 the RAW8xy width / height, [12] +0xFC the output buffer's macroblock count.

#include "sce/mpeg_internal.h"
#include "b3hw/hw.h"

extern char D_004B1D10[];   // [C] "load_chroma_intra_quantizer_matrix == 1"
extern char D_004B1D38[];   // [C] "load_chroma_non_intra_quantizer_matrix == 1"
extern char D_004B1D68[];   // [C] "odd number of field pictures"
extern char D_004B1D88[];   // [C] "unknown picture sutructure"
extern char D_004B1E38[];   // [C] "image buffer needs to be aligned to 64byte boundary(0x%08x)"
extern char D_004B1E78[];   // [C] "Need to re-setup libipu since sceMpegGetPicture was aborted\n"
extern char D_004B1EB8[];   // [C] "sceMpegGetPicture is aborted\n"
extern char D_004B1EF8[];   // [C] "the second field is missing"
extern char D_004B1F28[];   // [C] "vertical size > 2800"
extern char D_004B1F40[];   // [C] "_chroma_format needs to be 1: 420"
extern char D_004B1F68[];   // [C] "Unsupported profile/level"
extern char D_004B1F88[];   // [C] "_sequenceScalableExtension() is not implemented"
extern char D_004B1FB8[];   // [C] "Unknown Extension"
extern char D_004B1FD0[];   // [C] "_pictureSpatialScalableExtension is not supported"
extern char D_004B2008[];   // [C] "_pictureTemporalScalableExtension is not supported"
extern unsigned int D_00484040[];   // [C] .data, 0x40 bytes: the default intra quantiser matrix
extern unsigned int D_00484080[];   // [C] .data, 0x40 bytes of 0x10: the default non-intra matrix

extern "C" int func_00114E60(void);   // [C] DIntr: returns 1 when interrupts were enabled
extern "C" int EIntr(void);
extern "C" int scePrintf(const char *fmt, ...);
extern "C" void _ipuSetMPEG1(int on);
extern "C" void _sendIpuCommand(UnkStruct_sceMpegData *d, unsigned int cmd);
extern "C" void _waitIpuIdle(UnkStruct_sceMpegData *d);
extern "C" void _flushBuf(UnkStruct_sceMpegData *d, int n);
extern "C" unsigned int _nextBit(UnkStruct_sceMpegData *d, int n);
extern "C" int _nextHeader(UnkStruct_sceMpegData *d);
extern "C" void _extensionAndUserData(UnkStruct_sceMpegData *d);
extern "C" int _pictureData0(UnkStruct_sceMpegData *d);
extern "C" int _dispatchMpegCallback(UnkStruct_sceMpeg *mp, int *cbdata);
extern "C" int _dispatchMpegCbNodata(UnkStruct_sceMpeg *mp);
extern "C" void _Error(UnkStruct_sceMpegData *d, const char *msg);
extern "C" void _Error1(UnkStruct_sceMpegData *d, const char *fmt, int a2, int a3);
extern "C" void _alalcFree(UnkStruct_sceMpegAlalc *a);
extern "C" unsigned int _alalcAlloc(UnkStruct_sceMpegData *d, UnkStruct_sceMpegAlalc *a, unsigned int size,
                                    unsigned int align);
extern "C" int _updateRefImage(UnkStruct_sceMpegData *d, int second);
extern "C" void _outputFrame(UnkStruct_sceMpegData *d, int n, int show);
extern "C" int _dispRefImage(UnkStruct_sceMpegData *d, UnkStruct_sceMpegImage *img, int n);
extern "C" int _dispRefImageField(UnkStruct_sceMpegData *d, UnkStruct_sceMpegImage *a, UnkStruct_sceMpegImage *b,
                                  int n);
extern "C" void func_0010AA98(UnkStruct_sceMpegData *d, unsigned int addr, int size);
extern "C" int _getpic(UnkStruct_sceMpeg *mp);
extern "C" int _decodeOrSkip(UnkStruct_sceMpeg *mp, int count, int limit);
extern "C" int _decodeOrSkipFrame(UnkStruct_sceMpeg *mp, int count, int limit);
extern "C" int _decodeOrSkipField(UnkStruct_sceMpeg *mp, int count, int limit);
extern "C" int _sceMpegFlush(UnkStruct_sceMpeg *mp);
extern "C" void _lastFrame(UnkStruct_sceMpegData *d);
extern "C" int func_0010AB98(UnkStruct_sceMpegImage *img, int w, int h);
extern "C" void func_0010ACE0(UnkStruct_sceMpeg *mp);
extern "C" void func_0010AF88(UnkStruct_sceMpegImage *p0, UnkStruct_sceMpegImage *p1, UnkStruct_sceMpegImage *p2,
                              UnkStruct_sceMpegImage *p3, UnkStruct_sceMpegImage *p4, UnkStruct_sceMpegImage *p5,
                              UnkStruct_sceMpegImage *p6, UnkStruct_sceMpegImage *p7, UnkStruct_sceMpegImage *p8,
                              unsigned int b0, unsigned int b1, unsigned int b2, int w, int h);
extern "C" void func_0010B068(UnkStruct_sceMpegData *d, unsigned int cmd, unsigned int *table);

// [C] picture_coding_extension (after the 4-bit id): the four f_codes (+0x17C .. +0x188), intra_dc_precision into
// IPU_CTRL.IDP (bits 16-17), picture_structure (+0x18C; the first one seen also into +0xEC), +0x190 / +0x194 / +0x198
// (1 bit each), q_scale_type / intra_vlc_format / alternate_scan into IPU_CTRL bits 22 / 21 / 20 (read-modify-write
// each), +0x19C, one bit dropped, +0x1A0; with composite_display_flag its 20 bits are read and dropped.
// 0x00108120
extern "C" void _pictureCodingExtension(UnkStruct_sceMpegData *d)
{
    unsigned int v;

    d->field_17C = _nextBit(d, 4);
    d->field_180 = _nextBit(d, 4);
    d->field_184 = _nextBit(d, 4);
    d->field_188 = _nextBit(d, 4);
    v = _nextBit(d, 2);
    b3hw_write32(0x10002010, (b3hw_read32(0x10002010) & 0xFFFCFFFF) | (v << 16));
    v = _nextBit(d, 2);
    d->field_18C = v;
    if (d->field_CC[8] == 0) {   // +0xEC
        d->field_CC[8] = v;
    }
    d->field_190 = _nextBit(d, 1);
    d->field_194 = _nextBit(d, 1);
    d->field_198 = _nextBit(d, 1);
    v = _nextBit(d, 1);
    b3hw_write32(0x10002010, (b3hw_read32(0x10002010) & 0xFFBFFFFF) | (v << 22));
    v = _nextBit(d, 1);
    b3hw_write32(0x10002010, (b3hw_read32(0x10002010) & 0xFFDFFFFF) | (v << 21));
    v = _nextBit(d, 1);
    b3hw_write32(0x10002010, (b3hw_read32(0x10002010) & 0xFFEFFFFF) | (v << 20));
    d->field_19C = _nextBit(d, 1);
    _nextBit(d, 1);
    d->field_1A0 = _nextBit(d, 1);
    if (_nextBit(d, 1) != 0) {
        _nextBit(d, 1);
        _nextBit(d, 3);
        _nextBit(d, 1);
        _nextBit(d, 7);
        _nextBit(d, 8);
    }
}

// [C] extra_information_picture: while the next bit is 1, 8 bits are skipped.
// 0x00108310
extern "C" void _extrainfo(UnkStruct_sceMpegData *d)
{
    while (_nextBit(d, 1) != 0) {
        _flushBuf(d, 8);
    }
}

// [C] the running temporal position: +0x1C4 = +0x864 + tr (+ 0x400 when tr < 0 and +0x86C was 0, outside B
// pictures); +0x86C cleared for a nonzero tr outside B pictures; +0x868 = max(+0x868, +0x1C4) (always stored).
// 0x00108358
extern "C" void _updateTempTackData(UnkStruct_sceMpegData *d, int tr)
{
    int wrap;
    int t;
    int n;
    int m;

    wrap = 0;
    t = 0;
    if (d->field_168 != 3 && tr != 0) {
        if (tr < 0) {
            wrap = d->field_86C == 0;
        }
        d->field_86C = 0;
        t = tr;
    }
    n = d->field_864 + tr;
    d->field_1C4 = n;
    if (wrap != 0 && !(t < tr)) {
        d->field_1C4 = n + 0x400;
    }
    m = d->field_868;
    if (m < d->field_1C4) {
        m = d->field_1C4;
    }
    d->field_868 = m;
}

// [C] group_of_pictures_header: +0x100 = 0, +0x86C = 1, +0x864 = +0x868 + 1; the time code (1 + 5 + 6 + 1 + 6 + 6
// bits) dropped, closed_gop -> +0x1BC, broken_link -> +0x1C0; then _extensionAndUserData.
// 0x001083D0
extern "C" void _groupOfPicturesHeader(UnkStruct_sceMpegData *d)
{
    int v;

    d->field_100 = 0;
    v = d->field_868;
    d->field_86C = 1;
    d->field_864 = v + 1;
    _nextBit(d, 1);
    _nextBit(d, 5);
    _nextBit(d, 6);
    _nextBit(d, 1);
    _nextBit(d, 6);
    _nextBit(d, 6);
    d->field_1BC = _nextBit(d, 1);
    d->field_1C0 = _nextBit(d, 1);
    _extensionAndUserData(d);
}

// [C] quant_matrix_extension: load_intra (-> +0x858) and load_non_intra (-> +0x85C) each make the IPU read the matrix
// from the stream (SETIQ intra 0x50000000 / non-intra 0x58000000 between two waits); the chroma flags are errors.
// 0x00108470
extern "C" void _quantMatrixExtension(UnkStruct_sceMpegData *d)
{
    unsigned int v;

    v = _nextBit(d, 1);
    d->field_858 = v;
    if (v != 0) {
        _waitIpuIdle(d);
        _sendIpuCommand(d, 0x50000000);
        _waitIpuIdle(d);
    }
    v = _nextBit(d, 1);
    d->field_85C = v;
    if (v != 0) {
        _waitIpuIdle(d);
        _sendIpuCommand(d, 0x58000000);
        _waitIpuIdle(d);
    }
    if (_nextBit(d, 1) != 0) {
        _Error(d, D_004B1D10);
    }
    if (_nextBit(d, 1) != 0) {
        _Error(d, D_004B1D38);
    }
}

// [C] picture_display_extension: the number of frame centre offsets (progressive sequence: 1, or 2 / 3 by
// repeat_first_field (+0x19C) and +0x190; else frame pictures 2 / 3 by +0x19C, fields 1), each 16 bits horizontal
// (+0x1A4[i]) and vertical (+0x1B0[i]) with a marker bit after each.
// 0x00108538
extern "C" void _pictureDisplayExtension(UnkStruct_sceMpegData *d)
{
    int n;
    int i;

    if (d->field_154 != 0) {
        if (d->field_19C == 0) {
            n = 1;
        } else {
            n = d->field_190 != 0 ? 3 : 2;
        }
    } else if (d->field_18C == 3) {
        n = d->field_19C != 0 ? 3 : 2;
    } else {
        n = 1;
    }
    for (i = 0; i < n; i++) {
        d->field_1A4[i] = _nextBit(d, 16);
        _nextBit(d, 1);
        d->field_1B0[i] = _nextBit(d, 16);
        _nextBit(d, 1);
    }
}

// [C] copyright_extension: 1 + 8 + 1 + 7 + 1 + 20 + 1 + 22 + 1 + 22 bits, all dropped.
// 0x00108630
extern "C" void _copyrightExtension(UnkStruct_sceMpegData *d)
{
    _nextBit(d, 1);
    _nextBit(d, 8);
    _nextBit(d, 1);
    _nextBit(d, 7);
    _nextBit(d, 1);
    _nextBit(d, 20);
    _nextBit(d, 1);
    _nextBit(d, 22);
    _nextBit(d, 1);
    _nextBit(d, 22);
}

// [C] decodes the picture's data (_pictureData0) into the current record by picture structure (3 frame +0x1D8,
// 1 top +0x1E8, 2 bottom +0x1F8; another value: an error, +0x1D8); a frame picture while a first field waits (+0x138)
// is an error first. A nonzero result marks the record valid (+0x28 = 1) and is returned.
// 0x001086C0
extern "C" int _decPicture(UnkStruct_sceMpegData *d)
{
    UnkStruct_sceMpegImage *img;
    int r;

    if (d->field_18C == 3 && d->field_138 != 0) {
        _Error(d, D_004B1D68);
        d->field_138 = 0;
    }
    if (d->field_18C == 2) {
        img = d->field_1F8;
    } else if (d->field_18C == 1) {
        img = d->field_1E8;
    } else if (d->field_18C == 3) {
        img = d->field_1D8;
    } else {
        img = d->field_1D8;
        _Error(d, D_004B1D88);
    }
    r = _pictureData0(d);
    if (r != 0) {
        img->field_28 = 1;
    }
    return r;
}

// [C] queues size bytes at addr to the toIPU DMA: the size + 0x13 rounded down to 16 (C division, toward 0), then
// func_0010AA98 on the internal state. Returns 1.
// 0x00109EC0
extern "C" int func_00109EC0(UnkStruct_sceMpeg *mp, unsigned int addr, int size)
{
    func_0010AA98(mp->field_40, addr, (size + 0x13) / 16 * 16);
    return 1;
}

// [C] the API's picture request: +0xC8 = 1 (RGB32 through CSC), +0xF0 = the buffer's uncached address
// ((buf & 0x0FFFFFFF) | 0x20000000), +0xFC = its macroblock count, +0xF4 = +0xF8 = 0; then _getpic.
// 0x00109F00
extern "C" int sceMpegGetPicture(UnkStruct_sceMpeg *mp, void *buf, int n)
{
    UnkStruct_sceMpegData *d;

    d = mp->field_40;
    d->field_C8 = 1;
    d->field_CC[9] = ((unsigned int)buf & 0x0FFFFFFF) | 0x20000000;   // +0xF0
    d->field_CC[12] = n;                                              // +0xFC
    d->field_CC[10] = 0;                                              // +0xF4
    d->field_CC[11] = 0;                                              // +0xF8
    return _getpic(mp);
}

// [C] the same with +0xC8 = 0 (raw macroblocks copied by _cpr8).
// 0x00109F48
extern "C" int sceMpegGetPictureRAW8(UnkStruct_sceMpeg *mp, void *buf, int n)
{
    UnkStruct_sceMpegData *d;

    d = mp->field_40;
    d->field_CC[12] = n;
    d->field_CC[9] = ((unsigned int)buf & 0x0FFFFFFF) | 0x20000000;
    d->field_CC[10] = 0;
    d->field_C8 = 0;
    d->field_CC[11] = 0;
    return _getpic(mp);
}

// [C] the RAW8 form into a w x h macroblock buffer: +0xF8 = h << 4, +0xF0 the address, +0xFC = w * h, +0xF4 = w << 4,
// +0xC8 = 0.
// 0x00109F90
extern "C" int sceMpegGetPictureRAW8xy(UnkStruct_sceMpeg *mp, void *buf, int w, int h)
{
    UnkStruct_sceMpegData *d;

    d = mp->field_40;
    d->field_CC[11] = h << 4;
    d->field_CC[9] = ((unsigned int)buf & 0x0FFFFFFF) | 0x20000000;
    d->field_CC[12] = w * h;
    d->field_CC[10] = w << 4;
    d->field_C8 = 0;
    return _getpic(mp);
}

// [C] the stop request: +0x10A0 = 1 (the waits and _getpic end on it). Returns 1.
// 0x00109FE0
extern "C" int func_00109FE0(UnkStruct_sceMpeg *mp)
{
    mp->field_40->field_10A0 = 1;
    return 1;
}

// [C] +0xB4 = a3, +0xAC = a1, +0xB0 = a2 (the I / P / B decode limits _getpic passes on; -1 = all).
// 0x00109FF0
extern "C" void func_00109FF0(UnkStruct_sceMpeg *mp, int a1, int a2, int a3)
{
    UnkStruct_sceMpegData *d;

    d = mp->field_40;
    d->field_B4 = a3;
    d->field_AC = a1;
    d->field_B0 = a2;
}

// [C] *a1 = +0xAC, *a2 = +0xB0, *a3 = +0xB4.
// 0x0010A008
extern "C" void func_0010A008(UnkStruct_sceMpeg *mp, int *a1, int *a2, int *a3)
{
    UnkStruct_sceMpegData *d;

    d = mp->field_40;
    *a1 = d->field_AC;
    *a2 = d->field_B0;
    *a3 = d->field_B4;
}

// [C] 1 when +4 is 0.
// 0x0010A038
extern "C" int func_0010A038(UnkStruct_sceMpeg *mp)
{
    return mp->field_40->field_4 == 0;
}

// [C] +0x90 = a1 (64-bit), +0x8C = 1. Returns 1.
// 0x0010A1D8
extern "C" int func_0010A1D8(UnkStruct_sceMpeg *mp, unsigned long long a1)
{
    UnkStruct_sceMpegData *d;

    d = mp->field_40;
    d->field_90 = a1;
    d->field_8C = 1;
    return 1;
}

// [C] +0x90 = 0, +0x8C = 0. Returns 1.
// 0x0010A1F0
extern "C" int func_0010A1F0(UnkStruct_sceMpeg *mp)
{
    UnkStruct_sceMpegData *d;

    d = mp->field_40;
    d->field_90 = 0;
    d->field_8C = 0;
    return 1;
}

// [C] +0xF0 = a1. Returns 1.
// 0x0010A208
extern "C" int func_0010A208(UnkStruct_sceMpeg *mp, int a1)
{
    mp->field_40->field_CC[9] = a1;
    return 1;
}

// [C] +0xE4.
// 0x0010A218
extern "C" int func_0010A218(UnkStruct_sceMpeg *mp)
{
    return mp->field_40->field_CC[6];
}

// [C] +0xE8.
// 0x0010A228
extern "C" int func_0010A228(UnkStruct_sceMpeg *mp)
{
    return mp->field_40->field_CC[7];
}

// [C] the address of +0xCC.
// 0x0010A238
extern "C" int *func_0010A238(UnkStruct_sceMpeg *mp)
{
    return &mp->field_40->field_CC[0];
}

// [C] the address of +0xCC (the same as func_0010A238).
// 0x0010A248
extern "C" int *func_0010A248(UnkStruct_sceMpeg *mp)
{
    return &mp->field_40->field_CC[0];
}

// [C] +0x100 = a1; returns the old value.
// 0x0010A258
extern "C" int func_0010A258(UnkStruct_sceMpeg *mp, int a1)
{
    UnkStruct_sceMpegData *d;
    int old;

    d = mp->field_40;
    old = d->field_100;
    d->field_100 = a1;
    return old;
}

// [C] +0x110 = 1, +0x108 = a1 (64-bit; _getPtsDtsFlags hands it out as the next pts). Returns 1.
// 0x0010A268
extern "C" int func_0010A268(UnkStruct_sceMpeg *mp, long long a1)
{
    UnkStruct_sceMpegData *d;

    d = mp->field_40;
    d->field_110 = 1;
    d->field_108 = a1;
    return 1;
}

// [C] the allocator's free bytes: base + size - next free.
// 0x0010A328
extern "C" unsigned int func_0010A328(UnkStruct_sceMpegAlalc *a)
{
    return a->field_0 + a->field_4 - a->field_8;
}

// [C] decodes until a picture is output: the end flag cleared; the output buffer must be 64-byte aligned (else an
// error, -1); a pending stop request: a message, -1. Then +0x838 = 0 and per header (_nextHeader; negative: -1;
// headers are read on while a field picture of the other structure follows in an MPEG-2 stream, unless the previous
// _decodeOrSkip returned -1): 0 (sequence end) _sceMpegFlush and the end flag; 1 (I) the counters +0xB8 / +0xBC /
// +0xC0 cleared, decode or skip by +0xAC; 2 (P) by +0xBC / +0xB0; 3 / 4 by +0xC0 / +0xB4, each counter + 1. A stop
// request: a message, -1; a picture output (+0x838) or the end: 1.
// 0x0010A340
extern "C" int _getpic(UnkStruct_sceMpeg *mp)
{
    UnkStruct_sceMpegData *d;
    int r;
    int hdr;

    r = 0;
    hdr = 1;
    d = mp->field_40;
    d->field_0 = 0;
    if (d->field_CC[9] & 0x3F) {
        // [C] a3 is whatever the caller left there (sceMpegGetPicture: the internal state); the format uses one
        // argument
        _Error1(d, D_004B1E38, d->field_CC[9], 0);
        return -1;
    }
    if (d->field_10A0 != 0) {
        scePrintf(D_004B1E78);
        return -1;
    }
    d->field_838 = 0;
    for (;;) {
        if (r != -1) {
            for (;;) {
                hdr = _nextHeader(d);
                if (hdr < 0) {
                    return -1;
                }
                if (hdr == 0) {
                    break;
                }
                if (d->field_18C == d->field_CC[8]) {
                    break;
                }
                if (d->field_860 == 0) {
                    break;
                }
            }
        }
        switch (hdr) {
        case 0:
            _sceMpegFlush(mp);
            d->field_0 = 1;
            break;
        case 1:
            d->field_C0 = 0;
            d->field_BC = 0;
            d->field_B8 = 0;
            r = _decodeOrSkip(mp, 0, d->field_AC);
            d->field_B8 = d->field_B8 + 1;
            break;
        case 2:
            r = _decodeOrSkip(mp, d->field_BC, d->field_B0);
            d->field_BC = d->field_BC + 1;
            break;
        case 3:
        case 4:
            r = _decodeOrSkip(mp, d->field_C0, d->field_B4);
            d->field_C0 = d->field_C0 + 1;
            break;
        }
        if (d->field_10A0 != 0) {
            scePrintf(D_004B1EB8);
            return -1;
        }
        if (d->field_838 != 0) {
            return 1;
        }
        if (d->field_0 != 0) {
            return 1;
        }
    }
}

// [C] a frame picture: decoded when limit is -1 or count < limit (the first one clears the public frame count and
// sets +8), its records updated (_updateRefImage) and _decPicture run when they allow it (result: decoded and
// nonzero); else skipped (the records' result, the no-data callback). A stop request returns 0; else the picture is
// output (_outputFrame), a decoded field picture toggles +0x138, the public frame count = +0x130 - +0xC4, and without
// a waiting field +0x130 and +4 count up.
// 0x0010A4F8
extern "C" int _decodeOrSkipFrame(UnkStruct_sceMpeg *mp, int count, int limit)
{
    UnkStruct_sceMpegData *d;
    int skipped;
    int r;

    skipped = 0;
    d = mp->field_40;
    if (limit == -1 || count < limit) {
        if (d->field_8 == 0) {
            mp->field_8 = 0;
            d->field_8 = 1;
        }
        r = 0;
        if (_updateRefImage(d, 0) != 0) {
            r = _decPicture(d) != 0;
        }
    } else {
        r = _updateRefImage(d, 0);
        skipped = 1;
        _dispatchMpegCbNodata(mp);
    }
    if (d->field_10A0 != 0) {
        return 0;
    }
    _outputFrame(d, d->field_130, d->field_4);
    if (d->field_18C != 3 && skipped == 0) {
        d->field_138 = d->field_138 == 0;
    }
    mp->field_8 = d->field_130 - d->field_C4;
    if (d->field_138 != 0) {
        return r;
    }
    d->field_130 = d->field_130 + 1;
    d->field_4 = d->field_4 + 1;
    return r;
}

// [C] frame pictures (+0x18C == 3) through _decodeOrSkipFrame, field pictures through _decodeOrSkipField.
// 0x0010A618
extern "C" int _decodeOrSkip(UnkStruct_sceMpeg *mp, int count, int limit)
{
    if (mp->field_40->field_18C == 3) {
        return _decodeOrSkipFrame(mp, count, limit);
    }
    return _decodeOrSkipField(mp, count, limit);
}

// [C] a field pair: +0x138 = 0; decode when limit is -1 or count < limit (the first one clears the public frame
// count and sets +8); the first field's records updated and, when decoding, _decPicture. A stop request returns 0.
// +0x138 = 1, the next header: 0 (sequence end) flushes, sets the end flag and returns 0; the second field must have
// the other structure (top first by +0xEC == 1: bottom, else top), else -1. Its records (_updateRefImage 1) and
// decode (result 1 when decoded and nonzero), a stop request 0, then output, +0x138 = 0, the public frame count,
// +0x130 / +4 count up, a skipped pair calls the no-data callback.
// 0x0010A660
extern "C" int _decodeOrSkipField(UnkStruct_sceMpeg *mp, int count, int limit)
{
    UnkStruct_sceMpegData *d;
    int dec;
    int want;
    int r;

    dec = 0;
    d = mp->field_40;
    d->field_138 = 0;
    if (limit == -1 || count < limit) {
        dec = 1;
    }
    if (d->field_8 == 0) {
        mp->field_8 = 0;
        d->field_8 = 1;
    }
    if (_updateRefImage(d, 0) != 0 && dec != 0) {
        _decPicture(d);
    }
    if (d->field_10A0 != 0) {
        return 0;
    }
    d->field_138 = 1;
    if (_nextHeader(d) == 0) {
        _sceMpegFlush(mp);
        d->field_0 = 1;
        return 0;
    }
    want = d->field_CC[8] != 1 ? 1 : 2;
    if (d->field_18C != want) {
        return -1;
    }
    r = 0;
    if (_updateRefImage(d, 1) != 0 && dec != 0) {
        if (_decPicture(d) != 0) {
            r = 1;
        }
    }
    if (d->field_10A0 != 0) {
        return 0;
    }
    _outputFrame(d, d->field_130, d->field_4);
    d->field_138 = 0;
    mp->field_8 = d->field_130 - d->field_C4;
    d->field_130 = d->field_130 + 1;
    d->field_4 = d->field_4 + 1;
    if (dec == 0) {
        _dispatchMpegCbNodata(mp);
    }
    return r;
}

// [C] at the stream's end, when a picture is pending (+4) and decoding started (+8): the last reference picture is
// shown (_lastFrame), the public frame count updated, +4 = 0; returns 1, else 0.
// 0x0010A7D8
extern "C" int _sceMpegFlush(UnkStruct_sceMpeg *mp)
{
    UnkStruct_sceMpegData *d;

    d = mp->field_40;
    if (d->field_4 == 0) {
        return 0;
    }
    if (d->field_8 == 0) {
        return 0;
    }
    _lastFrame(d);
    mp->field_8 = d->field_130 - d->field_C4;
    d->field_4 = 0;
    return 1;
}

// [C] a waiting first field: an error; else the backward reference is shown (frame +0x1D4; fields +0x1E4 / +0x1F4).
// +0x138 = 0 either way.
// 0x0010A850
extern "C" void _lastFrame(UnkStruct_sceMpegData *d)
{
    int n;

    n = d->field_130;
    if (d->field_138 != 0) {
        _Error(d, D_004B1EF8);
        d->field_138 = 0;
        return;
    }
    if (d->field_18C == 3) {
        _dispRefImage(d, d->field_1D4, n - 1);
    } else {
        _dispRefImageField(d, d->field_1E4, d->field_1F4, n - 1);
    }
    d->field_138 = 0;
}

// [C] the picture's sizes: +0xC = w >> 4, +0x10 = h >> 4 (macroblocks), +4 = w, +8 = h. Returns 1.
// 0x0010AB98
extern "C" int func_0010AB98(UnkStruct_sceMpegImage *img, int w, int h)
{
    img->field_C = w >> 4;
    img->field_10 = h >> 4;
    img->field_4 = w;
    img->field_8 = h;
    return 1;
}

// [C] sequence_header (after its start code): +0xEC = 0; horizontal / vertical size (+0x13C / +0x140; vertical
// > 2800 is an error); bit_rate (+0x14C), vbv_buffer_size (+0x150); load_intra_quantizer_matrix (+0x858: from the
// stream by SETIQ, else the default D_00484040 through func_0010B068), load_non_intra (+0x85C, default D_00484080);
// _extensionAndUserData, then the picture buffers (func_0010ACE0).
// 0x0010ABB8
extern "C" void func_0010ABB8(UnkStruct_sceMpegData *d)
{
    unsigned int v;

    d->field_CC[8] = 0;   // +0xEC
    v = _nextBit(d, 32);
    d->field_13C = v >> 20;
    d->field_140 = (v >> 8) & 0xFFF;
    if (!(d->field_140 < 0xAF1)) {
        _Error(d, D_004B1F28);
    }
    v = _nextBit(d, 30);
    d->field_14C = v >> 12;
    d->field_150 = (v >> 1) & 0x3FF;
    v = _nextBit(d, 1);
    d->field_858 = v;
    if (v != 0) {
        _waitIpuIdle(d);
        _sendIpuCommand(d, 0x50000000);
        _waitIpuIdle(d);
    } else {
        func_0010B068(d, 0x50000000, D_00484040);
    }
    v = _nextBit(d, 1);
    d->field_85C = v;
    if (v != 0) {
        _waitIpuIdle(d);
        _sendIpuCommand(d, 0x58000000);
        _waitIpuIdle(d);
    } else {
        func_0010B068(d, 0x58000000, D_00484080);
    }
    _extensionAndUserData(d);
    func_0010ACE0(d->field_870);
}

// [C] the picture buffers for the sequence: an MPEG-1 stream (+0x860 == 0) gets the MPEG-2 defaults (frame
// pictures, +0x194 / +0x154 / +0x158 / +0x1A0 = 1, +0x15C = 5); macroblocks per row (+0x144) and per column (+0x148;
// an interlaced MPEG-2 sequence rounds to a pair of field rows). The same size as the public w / h: nothing; else
// the public size, the allocator back to its mark, three frame buffers of w * h * 1.5 bytes (64-byte aligned,
// +0x114 / +0x118 / +0x11C), the nine records' addresses (func_0010AF88) and sizes (frames w x h, fields w x h / 2).
// 0x0010ACE0
extern "C" void func_0010ACE0(UnkStruct_sceMpeg *mp)
{
    UnkStruct_sceMpegData *d;
    int mpeg2;
    int w;
    int h;
    int hh;
    unsigned int size;

    d = mp->field_40;
    mpeg2 = d->field_860;
    if (mpeg2 == 0) {
        d->field_18C = 3;
        d->field_194 = 1;
        d->field_15C = 5;
        d->field_154 = 1;
        d->field_158 = 1;
        d->field_1A0 = 1;
    }
    d->field_144 = (d->field_13C + 15) >> 4;
    if (mpeg2 != 0 && d->field_154 == 0) {
        d->field_148 = ((d->field_140 + 0x1F) >> 5) << 1;
    } else {
        d->field_148 = (d->field_140 + 15) >> 4;
    }
    h = d->field_148 << 4;
    w = d->field_144 << 4;
    if (w == mp->field_0 && h == mp->field_4) {
        return;
    }
    mp->field_4 = h;
    mp->field_0 = w;
    size = (unsigned int)w * ((unsigned int)h * 0x180) >> 8;
    hh = h >> 1;
    _alalcFree(&d->field_120);
    d->field_114 = _alalcAlloc(d, &d->field_120, size, 0x40);
    d->field_118 = _alalcAlloc(d, &d->field_120, size, 0x40);
    d->field_11C = _alalcAlloc(d, &d->field_120, size, 0x40);
    func_0010AF88(&d->field_200[0], &d->field_200[1], &d->field_200[2], &d->field_200[3], &d->field_200[4],
                  &d->field_200[5], &d->field_200[6], &d->field_200[7], &d->field_200[8], d->field_114, d->field_118,
                  d->field_11C, w, h);
    func_0010AB98(&d->field_200[0], w, h);
    func_0010AB98(&d->field_200[1], w, h);
    func_0010AB98(&d->field_200[2], w, h);
    func_0010AB98(&d->field_200[3], w, hh);
    func_0010AB98(&d->field_200[4], w, hh);
    func_0010AB98(&d->field_200[5], w, hh);
    func_0010AB98(&d->field_200[6], w, hh);
    func_0010AB98(&d->field_200[7], w, hh);
    func_0010AB98(&d->field_200[8], w, hh);
}

// [C] the records' buffer addresses, uncached ((b & 0x0FFFFFFF) | 0x20000000): frames p0 / p1 / p2 = b0 / b1 / b2,
// the first fields p3 / p4 / p5 the same, the second fields p6 / p7 / p8 = b + (w * h / 512) * 0x180 (half a frame).
// 0x0010AF88
extern "C" void func_0010AF88(UnkStruct_sceMpegImage *p0, UnkStruct_sceMpegImage *p1, UnkStruct_sceMpegImage *p2,
                              UnkStruct_sceMpegImage *p3, UnkStruct_sceMpegImage *p4, UnkStruct_sceMpegImage *p5,
                              UnkStruct_sceMpegImage *p6, UnkStruct_sceMpegImage *p7, UnkStruct_sceMpegImage *p8,
                              unsigned int b0, unsigned int b1, unsigned int b2, int w, int h)
{
    unsigned int half;
    unsigned int a0;
    unsigned int a1;
    unsigned int a2;

    half = (w * h) / 512 * 0x180;
    a0 = (b0 & 0x0FFFFFFF) | 0x20000000;
    a1 = (b1 & 0x0FFFFFFF) | 0x20000000;
    a2 = (b2 & 0x0FFFFFFF) | 0x20000000;
    p0->field_0 = a0;
    p1->field_0 = a1;
    p2->field_0 = a2;
    p3->field_0 = a0;
    p4->field_0 = a1;
    p5->field_0 = a2;
    p6->field_0 = ((b0 + half) & 0x0FFFFFFF) | 0x20000000;
    p7->field_0 = ((b1 + half) & 0x0FFFFFFF) | 0x20000000;
    p8->field_0 = ((b2 + half) & 0x0FFFFFFF) | 0x20000000;
}

// [C] a default quantiser matrix into the IPU: the stop-DMA callback (type 2), BCLR between two waits, the 4 qwords
// at table by toIPU DMA (MADR, QWC 4, CHCR 0x101 with interrupts off), the SETIQ command (cmd) and a wait, the
// restart-DMA callback (type 3).
// 0x0010B068
extern "C" void func_0010B068(UnkStruct_sceMpegData *d, unsigned int cmd, unsigned int *table)
{
    int cb[4];
    int was;

    cb[0] = 2;
    _dispatchMpegCallback(d->field_870, cb);
    _waitIpuIdle(d);
    b3hw_write32(0x10002000, 0);
    _waitIpuIdle(d);
    was = func_00114E60();
    b3hw_write32(0x1000B410, (unsigned int)table & 0x0FFFFFFF);
    b3hw_write32(0x1000B420, 4);
    b3hw_write32(0x1000B400, 0x101);
    if (was) {
        EIntr();
    }
    _sendIpuCommand(d, cmd);
    _waitIpuIdle(d);
    cb[0] = 3;
    _dispatchMpegCallback(d->field_870, cb);
}

// [C] sequence_extension: MPEG-2 (+0x860 = 1, IPU_CTRL.MP1 off); 28 bits: profile_and_level (0x48 / 0x58 / 0x44
// else an error), progressive_sequence (+0x154), chroma_format (+0x158; not 1 is an error), the size, bit rate and
// (from the next 16 bits) vbv extensions added to +0x13C / +0x140 (bits 12-13), +0x14C (bits 18+), +0x150 (bits 10+).
// 0x0010B148
extern "C" void _sequenceExtension(UnkStruct_sceMpegData *d)
{
    unsigned int v;
    unsigned int brx;
    unsigned int vx;
    unsigned int hx;
    unsigned int pl;
    unsigned int vbvx;
    unsigned int cf;
    int a0;
    int v1;
    int a2;
    int v0;

    d->field_860 = 1;
    _ipuSetMPEG1(0);
    v = _nextBit(d, 28);
    brx = (v >> 1) & 0xFFF;
    cf = (v >> 17) & 3;
    vx = (v >> 13) & 3;
    hx = (v >> 15) & 3;
    d->field_158 = cf;
    if (cf != 1) {
        _Error(d, D_004B1F40);
    }
    d->field_154 = (v >> 19) & 1;
    pl = v >> 20;
    vbvx = _nextBit(d, 16) >> 8;
    if (pl != 0x48 && pl != 0x58 && pl != 0x44) {
        _Error(d, D_004B1F68);
    }
    a0 = d->field_13C;
    v1 = d->field_140;
    a2 = d->field_14C;
    v0 = d->field_150;
    d->field_150 = v0 + (vbvx << 10);
    d->field_13C = (hx << 12) | (a0 & 0xFFF);
    d->field_140 = (vx << 12) | (v1 & 0xFFF);
    d->field_14C = a2 + (brx << 18);
}

// [C] sequence_display_extension: video_format (3 bits) dropped; with colour_description three 8-bit codes, the last
// (matrix_coefficients) into +0x15C; display_horizontal_size (14 bits) -> +0x160, a marker, display_vertical_size
// -> +0x164.
// 0x0010B278
extern "C" void func_0010B278(UnkStruct_sceMpegData *d)
{
    _nextBit(d, 3);
    if (_nextBit(d, 1) != 0) {
        _nextBit(d, 8);
        _nextBit(d, 8);
        d->field_15C = _nextBit(d, 8);
    }
    d->field_160 = _nextBit(d, 14);
    _nextBit(d, 1);
    d->field_164 = _nextBit(d, 14);
}

// [C] an error (not implemented).
// 0x0010B308
extern "C" void _sequenceScalableExtension(UnkStruct_sceMpegData *d)
{
    _Error(d, D_004B1F88);
}

// [C] the extension table's entry for unknown ids: an error.
// 0x0010B318
extern "C" void func_0010B318(UnkStruct_sceMpegData *d)
{
    _Error(d, D_004B1FB8);
}

// [C] picture spatial scalable extension (id 9): an error.
// 0x0010B328
extern "C" void func_0010B328(UnkStruct_sceMpegData *d)
{
    _Error(d, D_004B1FD0);
}

// [C] picture temporal scalable extension (id 10): an error.
// 0x0010B338
extern "C" void func_0010B338(UnkStruct_sceMpegData *d)
{
    _Error(d, D_004B2008);
}
