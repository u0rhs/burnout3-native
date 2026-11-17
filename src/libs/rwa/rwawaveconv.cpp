// rwawaveconv.o (librwacore.a) -- wave format conversion: the table of converters between pairs of formats, the search
// for a chain of them (with a 5-entry cache of found chains), sizes after conversion, and the PCM <-> VAG converter.
// Retail 0x299B00-0x29E72C [C E map]. Ported: the functions called in races14 (_RwaWaveConvVAG, _rwaWaveConvGetTargetSize,
// _rwaWaveConvGetOffsetLength, _RwaWaveConvFindPath). Not ported yet: the XADPCM / GCN ADPCM / PCM converters and
// their helpers, _RwaWaveConvVAGDecode / Encode / EncodeBlock, _rwaWaveConv 0x29DD00, _rwaWaveConvGetTargetOffset.
#include "rwa/rwacore.h"

extern "C" void *memcpy(void *dst, const void *src, unsigned int n);
extern "C" void *memset(void *dst, int c, unsigned int n);
extern "C" RwInt32 RwaUUIDCompare(const RwaUUID *a, const RwaUUID *b);
extern "C" RwInt32 RwaWaveFormatCompare(const RwaWaveFormat *a, const RwaWaveFormat *b);
extern "C" RwBool RwaWaveDefFormatWaveFormatCompare(const RwaWaveDefFormat *f, const RwaWaveFormat *fmt);
extern "C" void _rwaWaveDefFormatCopyToWaveFormat(const RwaWaveDefFormat *f, RwaWaveFormat *out, RwBool clear);
extern "C" void *func_00293430(RwUInt32 n, RwUInt32 size, RwUInt32 hint);   // RWA calloc (rwamemory.o)
extern "C" void _RwaWaveConvVAGDecode(RwUInt8 *src, RwInt16 *dst, RwUInt32 blocks);
extern "C" RwUInt32 _RwaWaveConvVAGEncode(RwInt16 *src, RwUInt32 bytes, RwUInt8 *dst, RwInt32 p3, RwInt32 p4,
                                          RwUInt32 dstBytes);
extern RwaUUID rwaWAVEFORMAT_PCM;
extern RwaUUID rwaSPU2WAVEID;

// One conversion (the rodata entries _supportedFormats points to, D_004C1228 ...), 0x10 bytes [C fields 0x29E198,
// 0x29E1DC, 0x29E274]. Not in the Revenge stabs.
struct UnkStruct_004C1228 {
    RwaWaveDefFormat *field_0;    // 0x0 the source format
    RwaWaveDefFormat *field_4;    // 0x4 the target format
    RwUInt32 field_8;             // 0x8 1: the conversion changes the sample rate (any target rate is reached)
    RwUInt32 field_C;
};
// A converter: (source format, source data, source offset, target format, target data or null, *target offset) ->
// the converted size; 0 on a failure [C call 0x29E2B4; _RwaWaveConvVAG].
// The source data is a word: an EE address only when the converter reads it (with target data); size queries pass
// other values (0x75800A8 in the a1races14 capture of _RwaWaveConvVAG, not in RAM).
typedef RwUInt32 (*UnkFn_0049EA00)(RwaWaveFormat *src, RwUInt32 srcData, RwUInt32 offset, RwaWaveFormat *dst,
                                   RwUInt8 *dstData, RwUInt32 *dstOffset);
// _supportedFormats [C name, retail_data_names.csv]: 0x33 entries {conversion, converter} [C data 0x49EA00].
struct UnkStruct_0049EA00 {
    UnkStruct_004C1228 *field_0;
    UnkFn_0049EA00 field_4;
};
extern UnkStruct_0049EA00 _supportedFormats[0x33];

// A cached chain, 0x4C bytes [C 0x29E4BC `* 0x4C`, fields 0x29E690-0x29E6D4].
struct UnkStruct_01E7D320 {
    RwaWaveFormat field_0;              // 0x00 the source format (its rate)
    RwaWaveFormat field_1C;             // 0x1C the target format
    RwUInt32 field_38;                  // 0x38 entries in the chain (0: unused)
    UnkStruct_0049EA00 *field_3C[4];    // 0x3C the chain
};
extern UnkStruct_01E7D320 D_01E7D320[5];   // the chain cache
extern RwUInt32 _mruCacheIndex;            // the cache entry the next chain goes to

// divu as the EE does it when the divisor is 0: quotient 0xFFFFFFFF [L: the R5900 / PCSX2 rule, as in rwawavedef.cpp].
static RwUInt32 func_0029AA80_inl1(RwUInt32 a, RwUInt32 b)
{
    return b ? a / b : 0xFFFFFFFF;
}

// The VAG converter: VAG -> PCM (7 / 2 bytes of PCM per byte of VAG) unless the source is PCM and the target an SPU2
// wave, then PCM -> VAG (2 / 7, each channel rounded up to 16-byte blocks). With target data, it converts from
// `offset` on (the target offset into *dstOffset); a multi-channel target is encoded channel by channel from a
// de-interleaved copy, then interleaved block by block. The full converted size (0 when the copy gets no memory).
// The PCM -> VAG path splits the source channels by the source's channel count and interleaves by the target's
// [C 0x29AB68, 0x29AC48]; kept as retail.
// 0x0029AA80
extern "C" RwUInt32 _RwaWaveConvVAG(RwaWaveFormat *src, RwUInt32 srcData, RwUInt32 offset, RwaWaveFormat *dst,
                                    RwUInt8 *dstData, RwUInt32 *dstOffset)
{
    RwUInt32 local;
    if (!dstOffset)
        dstOffset = &local;
    if (RwaUUIDCompare(src->dataType, &rwaWAVEFORMAT_PCM) != 0 || RwaUUIDCompare(dst->dataType, &rwaSPU2WAVEID) != 0) {
        *dstOffset = (offset * 7) >> 1;
        RwUInt32 size = (src->length * 7) >> 1;
        if (dstData) {
            dstData += *dstOffset;
            memset(dstData, 0, size - *dstOffset);
            srcData += offset;
            _RwaWaveConvVAGDecode((RwUInt8 *)srcData, (RwInt16 *)dstData, src->length >> 4);
        }
        return size;
    }
    RwUInt32 perChannel = func_0029AA80_inl1(src->length, src->noChannels);   // PCM bytes per channel
    RwUInt32 vagBytes = ((perChannel * 2) / 7 + 0xF) & ~0xFu;                    // VAG bytes per channel
    *dstOffset = (offset * 2) / 7;
    *dstOffset = (*dstOffset + 0xF) & ~0xFu;
    RwUInt32 size = src->noChannels * vagBytes;
    if (!dstData)
        return size;
    RwUInt8 *pcm = (RwUInt8 *)srcData;
    if (dst->noChannels >= 2) {
        pcm = (RwUInt8 *)func_00293430(1, src->length, 0x10806);
        if (!pcm)
            return 0;
        perChannel &= ~1u;
        for (RwUInt32 ch = 0; ch < src->noChannels; ch++) {
            RwInt16 *in = (RwInt16 *)(srcData + 2 * ch);   // an EE address here (target data given)
            RwInt16 *out = (RwInt16 *)(pcm + perChannel * ch);
            for (RwUInt32 n = perChannel; n != 0; n -= 2) {
                *out++ = *in;
                in = (RwInt16 *)((RwUInt8 *)in + 2 * src->noChannels);
            }
        }
    }
    RwUInt8 *from = pcm + offset;
    RwUInt32 remaining = perChannel - offset;
    for (RwUInt32 ch = 0; ch < src->noChannels; ch++) {
        RwUInt8 *in = from + perChannel * ch;
        RwUInt8 *out = dstData + *dstOffset + vagBytes * ch;
        memset(out, 0, vagBytes);
        _RwaWaveConvVAGEncode((RwInt16 *)in, remaining, out, 1, 0, vagBytes - *dstOffset);
    }
    if (dst->noChannels < 2)
        return size;
    RwUInt32 nch = dst->noChannels;
    for (RwUInt32 ch = 0; ch < nch; ch++) {
        RwUInt8 *out = pcm + 16 * ch;
        RwUInt8 *in = dstData + vagBytes * ch;
        for (RwUInt32 n = vagBytes; n != 0; n -= 0x10) {
            memcpy(out, in, 0x10);
            nch = dst->noChannels;
            in += 0x10;
            out += 16 * nch;
        }
    }
    memcpy(dstData, pcm, vagBytes * nch);
    func_00293440(pcm);
    return size;
}

// The first converter whose source (`target` 0) or target (1) format is `fmt`'s; null when none.
static RwaWaveDefFormat *func_0029E130_inl1(const RwaWaveFormat *fmt, RwBool target)
{
    for (RwUInt32 i = 0; i < 0x33; i++) {
        RwaWaveDefFormat *f = target ? _supportedFormats[i].field_0->field_4 : _supportedFormats[i].field_0->field_0;
        if (RwaWaveDefFormatWaveFormatCompare(f, fmt))
            return f;
    }
    return 0;
}

extern "C" RwUInt32 _RwaWaveConvFindPath(RwaWaveDefFormat *from, RwUInt32 fromRate, RwaWaveDefFormat *to,
                                         RwUInt32 toRate, UnkStruct_0049EA00 **out, UnkStruct_0049EA00 **work,
                                         RwUInt32 maxDepth, RwUInt32 depth, RwInt32 best);

// The size of `src`'s data converted to `dst` along the converter chain (*dstOffset: `offset` mapped the same way),
// or src's length (and the offset unchanged) when the formats are equal; 0 when a converter fails. With no chain the
// result and *dstOffset are what retail's stack held [C 0x29E2E8: the loop never ran]: the frame below keeps retail's
// addresses, so they are the same stale words.
// 0x0029E130
extern "C" RwUInt32 _rwaWaveConvGetOffsetLength(RwaWaveFormat *src, RwUInt32 srcData, RwaWaveFormat *dst,
                                                RwUInt32 offset, RwUInt32 *dstOffset)
{
    B3_RETAIL_FRAME(0x110);
    RwaWaveFormat *next = B3_FRAME_LOCAL(RwaWaveFormat, 0xA0);
    RwaWaveFormat *cur = B3_FRAME_LOCAL(RwaWaveFormat, 0xC0);
    RwUInt32 *work = B3_FRAME_LOCAL(RwUInt32, 0xE0);
    RwUInt32 *path = B3_FRAME_LOCAL(RwUInt32, 0xF0);
    RwUInt32 *nextOffset = B3_FRAME_LOCAL(RwUInt32, 0x10C);
    if (RwaWaveFormatCompare(src, dst) != 0) {
        if (dstOffset)
            *dstOffset = offset;
        return src->length;
    }
    RwaWaveDefFormat *from = func_0029E130_inl1(src, 0);
    RwaWaveDefFormat *to = func_0029E130_inl1(dst, 1);
    RwUInt32 n = _RwaWaveConvFindPath(from, src->sampleRate, to, dst->sampleRate, (UnkStruct_0049EA00 **)path,
                                      (UnkStruct_0049EA00 **)work, 4, 0, -1);
    memcpy(cur, src, sizeof(RwaWaveFormat));
    for (RwUInt32 i = 0; i < n; i++) {
        UnkStruct_0049EA00 *e = (UnkStruct_0049EA00 *)path[i];
        _rwaWaveDefFormatCopyToWaveFormat(e->field_0->field_4, next, 1);
        if (e->field_0->field_8 & 1)
            next->sampleRate = dst->sampleRate;
        else
            next->sampleRate = cur->sampleRate;
        next->length = e->field_4(cur, srcData, offset, next, 0, nextOffset);
        if (!next->length)
            return 0;
        offset = *nextOffset;
        memcpy(cur, next, sizeof(RwaWaveFormat));
    }
    RwUInt32 length = next->length;
    if (dstOffset)
        *dstOffset = *nextOffset;
    return length;
}

// 0x0029E0F0
extern "C" RwUInt32 _rwaWaveConvGetTargetSize(RwaWaveFormat *src, RwUInt32 srcData, RwaWaveFormat *dst)
{
    return _rwaWaveConvGetOffsetLength(src, srcData, dst, 0, 0);
}

// A chain of at most maxDepth converters from (from, fromRate) to (to, toRate), depth first; `work` holds the chain
// being tried (entries 0..depth-1), `best` the last index of the shortest chain found so far (-1: none). A level stops
// as soon as its depth reaches `best`. At depth 0 the chain cache is tried first, then a single converter; a chain found
// by the search goes into the cache (round robin). The result: at depth 0 the chain's length, deeper the last index of
// the chain found (0: none); the chain itself in out[]. Retail keeps `best` in its stack argument slot (the caller's
// sp + 0, the ninth argument) and updates it there [C 0x29E458, 0x29E674]: the frame below keeps retail's sp, so the
// slot is written as retail writes it (neither caller reads it back [C 0x29E22C, 0x29DDF0], but it is the caller's
// live frame).
// 0x0029E360
extern "C" RwUInt32 _RwaWaveConvFindPath(RwaWaveDefFormat *from, RwUInt32 fromRate, RwaWaveDefFormat *to,
                                         RwUInt32 toRate, UnkStruct_0049EA00 **out, UnkStruct_0049EA00 **work,
                                         RwUInt32 maxDepth, RwUInt32 depth, RwInt32 best)
{
    B3_RETAIL_FRAME(0x100);
    RwInt32 *bestSlot = B3_FRAME_LOCAL(RwInt32, 0x100);   // the caller's sp + 0
    if (depth == maxDepth)
        return 0;
    if (depth == 0) {
        RwaWaveFormat a, b;
        _rwaWaveDefFormatCopyToWaveFormat(from, &a, 1);
        a.sampleRate = fromRate;
        _rwaWaveDefFormatCopyToWaveFormat(to, &b, 1);
        b.sampleRate = toRate;
        for (RwUInt32 c = 0; c < 5; c++) {
            UnkStruct_01E7D320 *e = &D_01E7D320[c];
            if (e->field_38 && RwaWaveFormatCompare(&a, &e->field_0) && RwaWaveFormatCompare(&b, &e->field_1C)) {
                for (RwUInt32 k = 0; k < e->field_38; k++)
                    out[k] = e->field_3C[k];
                return e->field_38;
            }
        }
        for (RwUInt32 i = 0; i < 0x33; i++) {
            UnkStruct_004C1228 *d = _supportedFormats[i].field_0;
            if (from == d->field_0 && to == d->field_4 && (fromRate == toRate || (d->field_8 & 1))) {
                out[0] = &_supportedFormats[i];
                return 1;
            }
        }
    }
    for (RwUInt32 i = 0; i < 0x33; i++) {
        UnkStruct_0049EA00 *entry = &_supportedFormats[i];
        UnkStruct_004C1228 *d = entry->field_0;
        if (from != d->field_0)
            continue;
        RwBool unused = 1;
        for (RwUInt32 k = 0; k < depth; k++) {
            if (work[k] == entry) {
                unused = 0;
                break;
            }
        }
        if (!unused)
            continue;
        if (to == d->field_4 && (fromRate == toRate || (d->field_8 & 1))) {
            work[depth] = entry;
            memcpy(out, work, 4 * (depth + 1));
            return depth ? depth : 1;
        }
        work[depth] = entry;
        if ((RwInt32)depth == best)
            return 0;
        RwUInt32 rate = (entry->field_0->field_8 & 1) ? toRate : fromRate;
        RwUInt32 r = _RwaWaveConvFindPath(entry->field_0->field_4, rate, to, toRate, out, work, maxDepth, depth + 1,
                                          best);
        if (r) {
            best = r;
            *bestSlot = best;
        }
    }
    if (best == -1)
        return 0;
    if (depth != 0)
        return best;
    RwUInt32 count = best + 1;
    *bestSlot = count;
    UnkStruct_01E7D320 *e = &D_01E7D320[_mruCacheIndex];
    _rwaWaveDefFormatCopyToWaveFormat(from, &e->field_0, 1);
    _rwaWaveDefFormatCopyToWaveFormat(to, &e->field_1C, 1);
    e->field_38 = count;
    e->field_0.sampleRate = fromRate;
    e->field_1C.sampleRate = toRate;
    for (RwUInt32 k = 0; k < count; k++)
        e->field_3C[k] = out[k];
    _mruCacheIndex++;
    if (_mruCacheIndex == 5)
        _mruCacheIndex = 0;
    return count;
}
