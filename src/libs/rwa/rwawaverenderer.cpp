// rwawaverenderer.o (librwacore.a; truncated in the E map) -- the wave renderer stream node: pulls packets from its
// input, uploads each block into its audio feeds' sound-RAM ring (one feed per channel, 0x130 bytes each: a wave, its
// voice at +0x60), tracks which block each feed voice plays, and keeps renderers on one list in step. Retail
// 0x2AB160-0x2AD12C [C E map]. C port written from retail's code (audio1); proven by the checker on live captures.
//
// The renderer state (obj +0x20) [C _RwaWaveRendererStateInitialise 0x2AC470, UploadNextBlock 0x2AC610]: +0x10 the
// input connection, +0x38 the current packet, +0x3C ring blocks n, +0x40 the tracking entries (n x 0x18: 0x10 bytes
// of the packet's position info, +0x14 1 = block holds valid data), +0x44 feeds uploaded of the current block, +0x4C
// samples per block, +0x54 the play position kept while stopped / paused, +0x58 next block to fill, +0x5C the block
// of the current packet, +0x60 samples queued, +0x64 the stream position returned by GetStreamPos (0x10 bytes),
// +0x74 packets taken, +0x78 the NULL packet (its +0xC the tracking entry to repeat), +0x8C flags (1 got the last
// packet, 2 waiting for uploads, 4 NULL packet in use, 8 stopped, 0x10 held back by the sync, 0x20 EE-side waves,
// 0x40 last block marked), +0x90 the last packet's block, +0x94 the last valid block, +0x98 / +0x9C the feeds and
// their count, +0xA0 bytes per block per feed, +0xA4 samples per ring block, +0xA8 next feed to upload, +0xAC gain,
// +0xB0 the shared renderer list (linked at +0xB4), +0xBC / +0xC0 the start sync (tick, all ready), +0xC4 / +0xC8 /
// +0xCC the packet sync (most packets taken, tick, all equal).
#include "rwa/rwacore.h"

typedef void (*UnkFn_002AB160)(RwUInt32 packet, RwUInt32 arg);
typedef RwUInt32 (*UnkFn_002ACB50)(RwUInt8 *feed, void *arg);
extern RwUInt8 _rwaGstreamNodeMemory[0xC];
extern "C" void *memcpy(void *dst, const void *src, unsigned int n);
extern "C" RwUInt32 _rwaStreamNodeGetStatus(RwaObj *obj);
extern "C" RwaObj *_RwaStreamNodeGetParams(RwaObj *obj, RwUInt32 *p);
extern "C" void _RwaStreamNodeCreationComplete(RwaObj *obj);
extern "C" RwUInt32 func_002AD460(void *cb);
extern "C" RwUInt32 RwaCoreGetTickTime(void);
extern "C" RwBool _RwaSPU2WaveGetUploadFromIOPPending(void);
extern "C" RwBool RwaWaveUploadBusy(RwaWave *wave);
extern "C" RwaWave *_RwaOsWaveUploadForRenderer(RwaWave *wave, RwUInt32 offset, RwUInt32 src, RwUInt32 size);
extern "C" void _rwaSPU2VoiceSetLoop(RwUInt8 *v, RwBool loop);   // RwaSPU2Voice *
extern "C" RwUInt32 _RwaWaveRendererCreateAudioFeeds(RwaObj *obj, RwaObj *p1, RwUInt32 *params, RwUInt32 p3, RwUInt32 p4);
extern "C" void _RwaWaveRendererDestroyAudioFeeds(RwaObj *obj, RwUInt32 p1, RwUInt32 p2);

// The word at p + off.
static RwUInt32 &func_002AB160_inl1(void *p, RwUInt32 off)
{
    return *(RwUInt32 *)((RwUInt8 *)p + off);
}

// Tracking entry i's word at off.
static RwUInt32 &func_002AB160_inl2(RwUInt8 *st, RwUInt32 i, RwUInt32 off)
{
    return func_002AB160_inl1((void *)(func_002AB160_inl1(st, 0x40) + i * 24), off);
}

// Hands a packet back upstream: its completion {fn, arg} at +0x10.
static void func_002AB160_inl3(RwUInt32 pk)
{
    RwUInt32 *cb = (RwUInt32 *)func_002AB160_inl1((void *)pk, 0x10);
    (*(UnkFn_002AB160 *)&cb[0])(pk, cb[1]);
}

// Samples played by the first feed's voice: the kept position while it is stopped / paused (voice state 3 or 1),
// else the voice's live count (+0x14).
static RwUInt32 func_002AB160_inl4(RwUInt8 *st)
{
    RwUInt32 *q = (RwUInt32 *)func_002AB160_inl1((void *)func_002AB160_inl1(st, 0x98), 0x90);
    RwBool kept = (q[0] == 3 || q[0] == 1);
    return kept ? func_002AB160_inl1(st, 0x54) : q[5];
}

// Feed f's upload still running (EE-side waves: RwaWaveUploadBusy; IOP-side: the one IOP upload pending).
static RwBool func_002AB160_inl5(RwUInt8 *st, RwUInt8 *f)
{
    if (func_002AB160_inl1(st, 0x8C) & 0x20)
        return RwaWaveUploadBusy((RwaWave *)f);
    return _RwaSPU2WaveGetUploadFromIOPPending();
}

extern "C" void _RwaWaveRendererStateInitialise(RwUInt8 *st);
extern "C" void _RwaWaveRendererStop(RwaObj *obj);

// 0x002AB160
extern "C" RwaObj *_RwaWaveRendererSetFlush(RwaObj *obj, RwUInt32 param, RwBool *flush)
{
    RwUInt8 *st = (RwUInt8 *)func_002AB160_inl1(obj, 0x20);
    if (*flush) {
        if (func_002AB160_inl1(st, 0x38)) {
            func_002AB160_inl3(func_002AB160_inl1(st, 0x38));
            func_002AB160_inl1(st, 0x38) = 0;
        }
        _RwaWaveRendererStateInitialise(st);
        _RwaWaveRendererStop(obj);
    }
    return obj;
}

// 0x002AB1D0
extern "C" RwaObj *_RwaWaveRendererGetStatus(RwaObj *obj, RwUInt32 param, RwUInt32 *out)
{
    *out = _rwaStreamNodeGetStatus(obj);
    return obj;
}

extern "C" RwUInt32 _RwaWaveRendererStateGetPlayingPacketIndex(RwUInt8 *st);

// The stream position: the tracking entry of the block playing, its sample position advanced by the samples played
// into that block (played - block * samples per block).
// 0x002AB240
extern "C" RwaObj *_RwaWaveRendererGetStreamPos(RwaObj *obj, RwUInt32 param, RwUInt32 *out)
{
    RwUInt8 *st = (RwUInt8 *)func_002AB160_inl1(obj, 0x20);
    RwUInt32 i = _RwaWaveRendererStateGetPlayingPacketIndex(st);
    memcpy(st + 0x64, (void *)(func_002AB160_inl1(st, 0x40) + i * 24), 0x10);
    RwUInt32 played = func_002AB160_inl4(st);
    func_002AB160_inl1(st, 0x6C) = func_002AB160_inl1(st, 0x6C) + (played - i * func_002AB160_inl1(st, 0xA4));
    *out = (unsigned int)(st + 0x64);
    return obj;
}

// Getter 2: feed req[2]'s voice (feed 0 for a mono renderer), or 0 past the feeds.
// 0x002AB310
extern "C" RwaObj *func_002AB310(RwaObj *obj, RwUInt32 param, RwUInt32 *req)
{
    RwUInt8 *st = (RwUInt8 *)func_002AB160_inl1(obj, 0x20);
    RwUInt32 n = func_002AB160_inl1(st, 0x9C);
    RwUInt32 i = (n == 1) ? 0 : req[2];
    if (i < n)
        req[0] = func_002AB160_inl1(st, 0x98) + i * 0x130 + 0x60;
    else
        req[0] = 0;
    return obj;
}

extern "C" void _RwaWaveRendererStateForAllAudioFeeds(RwUInt8 *st, UnkFn_002ACB50 fn, void *arg);
extern "C" RwUInt8 *func_002ACB10(RwUInt8 *f, RwReal *gain);
extern "C" RwUInt8 *func_002ACB30(RwUInt8 *f, RwReal *freq);

// 0x002AB3A0
extern "C" RwaObj *_RwaWaveRendererSetGain(RwaObj *obj, RwUInt32 param, RwReal *gain)
{
    RwUInt8 *st = (RwUInt8 *)func_002AB160_inl1(obj, 0x20);
    RwReal g = *gain;
    _RwaWaveRendererStateForAllAudioFeeds(st, (UnkFn_002ACB50)func_002ACB10, &g);
    *(RwReal *)(st + 0xAC) = g;
    return obj;
}

// 0x002AB420
extern "C" RwaObj *_RwaWaveRendererSetFreq(RwaObj *obj, RwUInt32 param, RwUInt32 *freq)
{
    RwUInt8 *st = (RwUInt8 *)func_002AB160_inl1(obj, 0x20);
    RwReal f = (RwReal)*freq;
    _RwaWaveRendererStateForAllAudioFeeds(st, (UnkFn_002ACB50)func_002ACB30, &f);
    return obj;
}

// 0x002AB490
extern "C" RwaObj *_RwaWaveRendererGetParams(RwaObj *obj, RwUInt32 *p)
{
    _RwaStreamNodeGetParams(obj, p);
    p[3] = 2;
    p[7] = 0;
    p[8] = (unsigned int)_rwaGstreamNodeMemory;
    p[1] = 0;
    p[0] = 1;
    p[0xD] = 0;
    return obj;
}

extern "C" void _RwaWaveRendererStateUpdatePackets(RwUInt8 *st);
extern "C" void _RwaWaveRendererStart(RwaObj *obj);
extern "C" void _RwaWaveRendererPause(RwaObj *obj);
extern "C" void _RwaWaveRendererStateUpdatePacketTracking(RwUInt8 *st);
extern "C" RwUInt32 _RwaWaveRendererStateGetFreeBufferSpace(RwUInt8 *st);
extern "C" RwBool _RwaWaveRendererStateWavesUploading(RwUInt8 *st);
extern "C" RwUInt32 _RwaWaveRendererStateUploadNextBlock(RwUInt8 *st);
extern "C" RwBool _RwaWaveRendererStateHasPlayedLastValidPacket(RwUInt8 *st);
extern "C" RwBool _RwaWaveRendererStateHasPlayedLastPacket(RwUInt8 *st);
extern "C" RwBool _RwaWaveRendererStateGotLastPacket(RwUInt8 *st);

// Per update (status +0x68: 2 created, 3 stopped, 4 starting, 5 buffered, 6 playing, 7 past the last valid block,
// 8 stopping, 9 last packet in): packets, the process state (+0x60: 1 stop, 2 play, 3 pause), tracking, then one
// block upload when there is ring space and no upload running, and the status steps; a stopped renderer leaves its
// list (+0x14 / +0x18).
// 0x002AB500
extern "C" RwaObj *_RwaWaveRendererFinishExecute(RwaObj *obj)
{
    RwUInt8 *st = (RwUInt8 *)func_002AB160_inl1(obj, 0x20);
    RwUInt32 &status = func_002AB160_inl1(obj, 0x68);
    if (status == 2)
        _RwaStreamNodeCreationComplete(obj);
    _RwaWaveRendererStateUpdatePackets(st);
    RwUInt32 ps = func_002AB160_inl1(obj, 0x60);
    if (ps == 3) {
        if (status != 5)
            _RwaWaveRendererPause(obj);
    } else if (ps == 2) {
        if (status != 6 && status != 9)
            _RwaWaveRendererStart(obj);
    } else if (ps == 1) {
        if (status != 3)
            _RwaWaveRendererStop(obj);
    }
    _RwaWaveRendererStateUpdatePacketTracking(st);
    if (status == 8) {
        RwUInt32 k = *(RwUInt32 *)func_002AB160_inl1((void *)func_002AB160_inl1(st, 0x98), 0x90);
        if (k == 3 || k == 1)
            status = 3;
    }
    if (status == 3 || status == 8) {
        if (func_002AB160_inl1(obj, 0x14)) {
            *(RwUInt32 *)func_002AB160_inl1(obj, 0x18) = func_002AB160_inl1(obj, 0x14);
            ((RwUInt32 *)func_002AB160_inl1(obj, 0x14))[1] = func_002AB160_inl1(obj, 0x18);
            func_002AB160_inl1(obj, 0x18) = 0;
            func_002AB160_inl1(obj, 0x14) = 0;
        }
        return obj;
    }
    RwUInt32 space = _RwaWaveRendererStateGetFreeBufferSpace(st);
    RwBool busy = _RwaWaveRendererStateWavesUploading(st);
    RwUInt32 up = 0;
    if (space && !busy) {
        up = _RwaWaveRendererStateUploadNextBlock(st);
        space -= up;
    }
    switch (status) {
    case 9:
        if (_RwaWaveRendererStateHasPlayedLastPacket(st))
            func_002AB160_inl1(obj, 0x60) = 1;
        break;
    case 6:
        if (_RwaWaveRendererStateHasPlayedLastValidPacket(st))
            status = 7;
        else if (!space || up)
            break;
        // fall through
    case 7:
        if (_RwaWaveRendererStateGotLastPacket(st))
            status = 9;
        break;
    case 4:
        if (!space)
            status = 5;
        break;
    }
    return obj;
}

// Every feed voice to state 1 (stopped), flag 8; status 8.
// 0x002AC170
extern "C" void _RwaWaveRendererStop(RwaObj *obj)
{
    RwUInt8 *st = (RwUInt8 *)func_002AB160_inl1(obj, 0x20);
    func_002AB160_inl1(st, 0x54) = 0;
    RwUInt8 *f = (RwUInt8 *)func_002AB160_inl1(st, 0x98);
    for (RwUInt32 i = 0; i < func_002AB160_inl1(st, 0x9C); i++) {
        *(RwUInt32 *)func_002AB160_inl1(f, 0x90) = 1;
        if (!f)
            break;
        f += 0x130;
    }
    func_002AB160_inl1(st, 0x8C) |= 8;
    func_002AB160_inl1(obj, 0x68) = 8;
}

// Unless already stopped / paused: the play position kept, every feed voice to state 1; status 4.
// 0x002AC1E0
extern "C" void _RwaWaveRendererPause(RwaObj *obj)
{
    RwUInt8 *st = (RwUInt8 *)func_002AB160_inl1(obj, 0x20);
    RwUInt32 *q = (RwUInt32 *)func_002AB160_inl1((void *)func_002AB160_inl1(st, 0x98), 0x90);
    if (!(q[0] == 3 || q[0] == 1)) {
        func_002AB160_inl1(st, 0x54) = q[5];
        RwUInt8 *f = (RwUInt8 *)func_002AB160_inl1(st, 0x98);
        for (RwUInt32 i = 0; i < func_002AB160_inl1(st, 0x9C); i++) {
            *(RwUInt32 *)func_002AB160_inl1(f, 0x90) = 1;
            if (!f)
                break;
            f += 0x130;
        }
    }
    func_002AB160_inl1(obj, 0x68) = 4;
}

// Start (from stopped / paused): once per tick the renderers on the list agree whether all have queued samples
// (+0x60); when they have, each feed voice starts at the kept position (looping, state 2) -> status 6; else 4.
// Already playing (not stopped): status 6.
// 0x002AC280
extern "C" void _RwaWaveRendererStart(RwaObj *obj)
{
    RwUInt8 *st = (RwUInt8 *)func_002AB160_inl1(obj, 0x20);
    RwUInt32 *q = (RwUInt32 *)func_002AB160_inl1((void *)func_002AB160_inl1(st, 0x98), 0x90);
    RwBool kept = (q[0] == 3 || q[0] == 1);
    if (!kept && !(func_002AB160_inl1(st, 0x8C) & 8)) {
        func_002AB160_inl1(obj, 0x68) = 6;
        return;
    }
    RwUInt32 t = RwaCoreGetTickTime();
    if (func_002AB160_inl1(st, 0xBC) < t) {
        RwUInt8 *head = (RwUInt8 *)func_002AB160_inl1(st, 0xB0);
        RwUInt32 ready = 1;   // retail leaves it unset for an empty list [C 0x2AC348]
        for (RwUInt8 *l = (RwUInt8 *)func_002AB160_inl1(head, 0); l != head; l = (RwUInt8 *)func_002AB160_inl1(l, 0)) {
            RwUInt8 *r = l - 0xB4;
            if (!func_002AB160_inl1(l - 0x54, 0)) {
                ready = 0;
                break;
            }
            ready = 1;
            if (!r)
                break;
        }
        head = (RwUInt8 *)func_002AB160_inl1(st, 0xB0);
        for (RwUInt8 *l = (RwUInt8 *)func_002AB160_inl1(head, 0); l != head; l = (RwUInt8 *)func_002AB160_inl1(l, 0)) {
            func_002AB160_inl1(l, 8) = t;
            func_002AB160_inl1(l, 0xC) = ready;
            if (!(l - 0xB4))
                break;
        }
    }
    if (!func_002AB160_inl1(st, 0xC0)) {
        func_002AB160_inl1(obj, 0x68) = 4;
        return;
    }
    RwUInt8 *f = (RwUInt8 *)func_002AB160_inl1(st, 0x98);
    for (RwUInt32 i = 0; i < func_002AB160_inl1(st, 0x9C); i++) {
        *(RwUInt16 *)(f + 0xBC) |= 0x80;
        ((RwUInt32 *)func_002AB160_inl1(f, 0x90))[6] = func_002AB160_inl1(st, 0x54);
        _rwaSPU2VoiceSetLoop(f + 0x60, 1);
        if (!f)
            break;
        f += 0x130;
    }
    f = (RwUInt8 *)func_002AB160_inl1(st, 0x98);
    for (RwUInt32 i = 0; i < func_002AB160_inl1(st, 0x9C); i++) {
        *(RwUInt32 *)func_002AB160_inl1(f, 0x90) = 2;
        if (!f)
            break;
        f += 0x130;
    }
    func_002AB160_inl1(st, 0x8C) &= ~8u;
    func_002AB160_inl1(obj, 0x68) = 6;
}

// 0x002AC470
extern "C" void _RwaWaveRendererStateInitialise(RwUInt8 *st)
{
    func_002AB160_inl1(st, 0x8C) = 0;
    func_002AB160_inl1(st, 0x90) = 0;
    func_002AB160_inl1(st, 0x94) = 0;
    func_002AB160_inl1(st, 0x54) = 0;
    func_002AB160_inl1(st, 0x58) = 0;
    func_002AB160_inl1(st, 0x60) = 0;
    func_002AB160_inl1(st, 0x64) = 0;
    func_002AB160_inl1(st, 0x68) = 3;
    func_002AB160_inl1(st, 0x6C) = 0;
    func_002AB160_inl1(st, 0x70) = 2;
    func_002AB160_inl1(st, 0xA8) = 0;
    func_002AB160_inl1(st, 0xAC) = 0x3F800000;
    func_002AB160_inl1(st, 0xC0) = 0;
    func_002AB160_inl1(st, 0xBC) = 0;
    func_002AB160_inl1(st, 0x5C) = 0;
    func_002AB160_inl1(st, 0x44) = 0;
    func_002AB160_inl1(st, 0x74) = 0;
    func_002AB160_inl1(st, 0xC4) = 0;
    func_002AB160_inl1(st, 0xC8) = 0;
    for (RwUInt32 i = 0; i < func_002AB160_inl1(st, 0x3C); i++) {
        func_002AB160_inl2(st, i, 0) = 0;
        func_002AB160_inl2(st, i, 4) = 3;
        func_002AB160_inl2(st, i, 8) = 0;
        func_002AB160_inl2(st, i, 0xC) = 2;
        func_002AB160_inl2(st, i, 0x10) = 0;
        func_002AB160_inl2(st, i, 0x14) = 0;
    }
}

// 0x002AC540
extern "C" RwBool _RwaWaveRendererStateHasPlayedLastValidPacket(RwUInt8 *st)
{
    if (!(func_002AB160_inl1(st, 0x8C) & 4))
        return 0;
    return !func_002AB160_inl2(st, func_002AB160_inl1(st, 0x94), 0x14);
}

// The last packet's block and the one before it both played (invalid).
// 0x002AC580
extern "C" RwBool _RwaWaveRendererStateHasPlayedLastPacket(RwUInt8 *st)
{
    RwUInt32 n = func_002AB160_inl1(st, 0x90);
    RwUInt32 prev = (n - 1 + func_002AB160_inl1(st, 0x3C)) % func_002AB160_inl1(st, 0x3C);
    if (func_002AB160_inl2(st, n, 0x14))
        return 0;
    if (func_002AB160_inl2(st, prev, 0x14))
        return 0;
    return 1;
}

// 0x002AC600
extern "C" RwBool _RwaWaveRendererStateGotLastPacket(RwUInt8 *st)
{
    return (func_002AB160_inl1(st, 0x8C) & 1) != 0;
}

// Uploads the current packet's block (or, with no packet, the NULL packet repeating the previous block's position)
// into ring block +0x5C of every feed (resuming at feed +0xA8 while a feed's upload runs); when all feeds have it, its
// tracking entry is filled and marked valid. Returns 1 when a block of data was completed.
// 0x002AC610
extern "C" RwUInt32 _RwaWaveRendererStateUploadNextBlock(RwUInt8 *st)
{
    RwUInt32 done = 0;
    RwUInt32 fl = func_002AB160_inl1(st, 0x8C);
    if (fl & 2)
        return 0;
    if (fl & 0x10)
        return 0;
    RwUInt8 *pk = (RwUInt8 *)func_002AB160_inl1(st, 0x38);
    RwBool null = 0;
    RwUInt32 blk;
    if (!pk) {
        null = 1;
        pk = st + 0x78;
        if (!(fl & 1) || (fl & 0x40))
            blk = func_002AB160_inl1(st, 0x58);
        else
            blk = func_002AB160_inl1(st, 0x5C);
        RwUInt32 prev = blk ? blk - 1 : func_002AB160_inl1(st, 0x3C) - 1;
        func_002AB160_inl1(pk, 0xC) = func_002AB160_inl1(st, 0x40) + prev * 24;
        func_002AB160_inl1(st, 0x94) = prev;
        if (func_002AB160_inl1(st, 0x8C) & 4) {
            if (func_002AB160_inl1(st, 0x44) == func_002AB160_inl1(st, 0x9C))
                func_002AB160_inl1(st, 0x44) = 0;
        } else {
            func_002AB160_inl1(st, 0x44) = 0;
        }
        done = 0;
        func_002AB160_inl1(st, 0x8C) |= 4;
    } else {
        blk = func_002AB160_inl1(st, 0x5C);
        if (func_002AB160_inl1(pk, 0) & 8) {
            func_002AB160_inl1(st, 0x8C) = fl | 1;
            func_002AB160_inl1(st, 0x90) = blk;
        }
        if (func_002AB160_inl1(st, 0x8C) & 4)
            func_002AB160_inl1(st, 0x8C) &= ~4u;
    }
    RwUInt32 i = func_002AB160_inl1(st, 0xA8);
    RwUInt32 dst = blk * func_002AB160_inl1(st, 0xA0);
    while (i < func_002AB160_inl1(st, 0x9C)) {
        RwUInt8 *f = (RwUInt8 *)(func_002AB160_inl1(st, 0x98) + i * 0x130);
        if (func_002AB160_inl5(st, f)) {
            done = 0;
            break;
        }
        _RwaOsWaveUploadForRenderer((RwaWave *)f, dst, func_002AB160_inl1(pk, 4) + i * func_002AB160_inl1(st, 0xA0),
                                    func_002AB160_inl1(st, 0xA0));
        RwUInt32 n = func_002AB160_inl1(st, 0x44);
        if (!(n < func_002AB160_inl1(st, 0x9C)))
            break;
        i++;
        func_002AB160_inl1(st, 0x44) = n + 1;
    }
    if (i == func_002AB160_inl1(st, 0x9C))
        i = 0;
    if (func_002AB160_inl1(st, 0x44) == func_002AB160_inl1(st, 0x9C)) {
        memcpy((void *)(func_002AB160_inl1(st, 0x40) + blk * 24), (void *)func_002AB160_inl1(pk, 0xC), 0x10);
        RwUInt32 n = blk + 1;
        if (n == func_002AB160_inl1(st, 0x3C))
            n = 0;
        func_002AB160_inl1(st, 0x58) = n;
        if (!null) {
            done = 1;
            func_002AB160_inl2(st, blk, 0x14) = 1;
            func_002AB160_inl1(st, 0x60) = func_002AB160_inl1(st, 0x60) + func_002AB160_inl1(st, 0x4C);
            func_002AB160_inl1(st, 0x5C) = func_002AB160_inl1(st, 0x58);
        } else if ((func_002AB160_inl1(st, 0x8C) & 1) && !(func_002AB160_inl1(st, 0x8C) & 0x40)) {
            done = 1;
            func_002AB160_inl2(st, blk, 0x14) = 1;
            func_002AB160_inl1(st, 0x8C) |= 0x40;
        }
    }
    func_002AB160_inl1(st, 0xA8) = i;
    return done;
}

// Packets: a packet every feed has taken goes back upstream once no upload runs (else flag 2), +0x74 counting them;
// then, with no packet, the renderers on the list sync once per tick (the most packets taken, and whether all took
// as many) and this one pulls the next packet from its input when all are level or it is behind (else flag 0x10).
// 0x002AC8E0
extern "C" void _RwaWaveRendererStateUpdatePackets(RwUInt8 *st)
{
    if (func_002AB160_inl1(st, 0x44) == func_002AB160_inl1(st, 0x9C) && func_002AB160_inl1(st, 0x38)) {
        RwBool busy = 0;
        RwUInt8 *f = (RwUInt8 *)func_002AB160_inl1(st, 0x98);
        for (RwUInt32 i = 0; i < func_002AB160_inl1(st, 0x9C); i++, f += 0x130) {
            if (func_002AB160_inl5(st, f)) {
                busy = 1;
                break;
            }
        }
        if (busy) {
            func_002AB160_inl1(st, 0x8C) |= 2;
        } else {
            func_002AB160_inl3(func_002AB160_inl1(st, 0x38));
            func_002AB160_inl1(st, 0x38) = 0;
            func_002AB160_inl1(st, 0x8C) &= ~2u;
            func_002AB160_inl1(st, 0x74) = func_002AB160_inl1(st, 0x74) + 1;
        }
    }
    if (func_002AB160_inl1(st, 0x38))
        return;
    RwUInt32 t = RwaCoreGetTickTime();
    if (func_002AB160_inl1(st, 0xC8) < t) {
        RwUInt32 most = func_002AB160_inl1(st, 0x74);
        RwUInt32 level = 1;
        RwUInt8 *head = (RwUInt8 *)func_002AB160_inl1(st, 0xB0);
        for (RwUInt8 *l = (RwUInt8 *)func_002AB160_inl1(head, 0); l != head; l = (RwUInt8 *)func_002AB160_inl1(l, 0)) {
            RwUInt32 n = func_002AB160_inl1(l - 0x40, 0);
            if (most != n) {
                level = 0;
                if (most < n)
                    most = n;
            }
            if (!(l - 0xB4))
                break;
        }
        head = (RwUInt8 *)func_002AB160_inl1(st, 0xB0);
        for (RwUInt8 *l = (RwUInt8 *)func_002AB160_inl1(head, 0); l != head; l = (RwUInt8 *)func_002AB160_inl1(l, 0)) {
            func_002AB160_inl1(l, 0x18) = level;
            func_002AB160_inl1(l, 0x14) = t;
            func_002AB160_inl1(l, 0x10) = most;
            if (!(l - 0xB4))
                break;
        }
    }
    RwBool go = func_002AB160_inl1(st, 0xCC) || func_002AB160_inl1(st, 0x74) < func_002AB160_inl1(st, 0xC4);
    if (go) {
        func_002AB160_inl1(st, 0x38) = func_002AD460(st + 0x10);
        if (func_002AB160_inl1(st, 0x38))
            func_002AB160_inl1(st, 0x44) = 0;
        func_002AB160_inl1(st, 0x8C) &= ~0x10u;
    } else {
        func_002AB160_inl1(st, 0x8C) |= 0x10;
    }
}

// 0x002ACB10
extern "C" RwUInt8 *func_002ACB10(RwUInt8 *f, RwReal *gain)
{
    *(RwUInt16 *)(f + 0xBC) |= 2;
    *(RwReal *)(f + 0x100) = *gain;
    return f;
}

// 0x002ACB30
extern "C" RwUInt8 *func_002ACB30(RwUInt8 *f, RwReal *freq)
{
    *(RwUInt16 *)(f + 0xBC) |= 1;
    *(RwReal *)(f + 0xF8) = *freq;
    return f;
}

// fn(feed, arg) for each feed until one returns 0.
// 0x002ACB50
extern "C" void _RwaWaveRendererStateForAllAudioFeeds(RwUInt8 *st, UnkFn_002ACB50 fn, void *arg)
{
    RwUInt8 *f = (RwUInt8 *)func_002AB160_inl1(st, 0x98);
    for (RwUInt32 i = 0; i < func_002AB160_inl1(st, 0x9C); i++, f += 0x130)
        if (!fn(f, arg))
            break;
}

// 0x002ACBD0
extern "C" RwBool _RwaWaveRendererStateWavesUploading(RwUInt8 *st)
{
    RwUInt8 *f = (RwUInt8 *)func_002AB160_inl1(st, 0x98);
    for (RwUInt32 i = 0; i < func_002AB160_inl1(st, 0x9C); i++, f += 0x130)
        if (func_002AB160_inl5(st, f))
            return 1;
    return 0;
}

// Ring blocks free for upload: between the next block to fill and the block playing (all n before any samples were
// queued).
// 0x002ACC70
extern "C" RwUInt32 _RwaWaveRendererStateGetFreeBufferSpace(RwUInt8 *st)
{
    RwUInt32 next = func_002AB160_inl1(st, 0x58);
    RwUInt32 cur = func_002AB160_inl4(st) / func_002AB160_inl1(st, 0xA4);
    if (!func_002AB160_inl1(st, 0x60))
        return func_002AB160_inl1(st, 0x3C);
    if (next < cur)
        return cur - next;
    if (cur < next)
        return cur + (func_002AB160_inl1(st, 0x3C) - next);
    return 0;
}

// Blocks the voice has moved past (from the next block to fill up to the one playing) lose their valid mark.
// 0x002ACE00
extern "C" void _RwaWaveRendererStateUpdatePacketTracking(RwUInt8 *st)
{
    RwUInt32 cur = func_002AB160_inl4(st) / func_002AB160_inl1(st, 0xA4);
    RwUInt32 i = func_002AB160_inl1(st, 0x58);
    if (i < cur) {
        for (; i < cur; i++)
            func_002AB160_inl2(st, i, 0x14) = 0;
    } else if (cur < i) {
        for (; i < func_002AB160_inl1(st, 0x3C); i++)
            func_002AB160_inl2(st, i, 0x14) = 0;
        for (i = 0; i < cur; i++)
            func_002AB160_inl2(st, i, 0x14) = 0;
    }
}

// 0x002ACF10
extern "C" RwUInt32 _RwaWaveRendererStateGetPlayingPacketIndex(RwUInt8 *st)
{
    return func_002AB160_inl4(st) / func_002AB160_inl1(st, 0xA4);
}

// ---- creation / teardown ----
extern RwaObjDef D_01E80FA0;               // the wave renderer object definition (static struct)
extern RwaObjDefInterfaceInfo D_01E80F80;  // its interface info [G type]
extern RwLinkList _waverendererAliasInfo;
extern RwaInputParamDef _waverendererInputParams[5];
extern RwaOutputParamDef _waverendererOutputParams[6];
extern RwUInt8 _rendererMiscData[0x10];
extern RwaUUID rwaWAVERENDERERID;
extern RwaUUID rwaSTREAMRENDERERINTERFACEID;
extern RwaUUID rwaSPU2OUTPUTID;
extern RwaUUID rwaSPU2VOICEID;
extern RwUInt32 D_01E81004;                // the global NULL packet (IOP memory)
extern RwUInt32 D_01E81008;                // its size
extern const char D_004C1830[];            // the size-mismatch message
extern "C" int printf(const char *fmt, ...);
extern "C" void *memset(void *dst, int c, unsigned int n);
extern "C" RwUInt32 sceSifAllocSysMemory(RwInt32 mode, RwInt32 size, RwUInt32 addr);
extern "C" void RwaRPCTransfer(RwUInt32 iopAddr, void *ee, RwUInt32 size, RwUInt32 op, RwUInt32 unused, RwUInt32 p5,
                               RwUInt32 p6, RwUInt32 p7);
extern "C" RwaObjDef *_rwaObjDefFindByUUID(RwaObjDef *parent, const RwaUUID *uuid, RwaObjDef **out);
typedef void (*UnkFn_002ACF60)(RwaObj *obj, RwUInt32 arg);
extern "C" void _rwaObjDefFreeObj(RwaObj *obj, UnkFn_002ACF60 freeFn, RwUInt32 arg);
extern "C" RwaObj *_RwaStreamNodeCreate(RwaObj *obj, RwaObj *p1, void *params);
extern "C" RwInt32 _RwaStreamNodeDestroy(RwaObj *obj);   // [C] reads a0 only
extern "C" RwaObjInterfaceDef *RwaObjInterfaceDefFindByUUID(RwaUUID *id);
extern "C" RwaObjInterface *RwaObjInterfaceAttach(RwaObjInterfaceDef *intf, RwaObjDef *def, RwUInt32 p2, RwUInt32 p3);
extern "C" RwInt32 RwaObjInterfaceRemove(RwaObjInterface *intf);
extern "C" RwBool RwaObjIntSetParamUInt32(RwaObj *obj, RwaObjInterface *intf, RwUInt32 index, RwUInt32 v);
extern "C" RwaObjDef *RwaObjDefCreate(RwaObjDef *parent, RwUInt32 p1, RwaObjDef *def, void *intfInfo, void *aliasInfo,
                                      RwUInt32 hint);
extern "C" RwaObjDef *RwaObjDefAssignID(RwaObjDef *def, RwaUUID *id, const char *name);
extern "C" RwaObjDef *RwaObjDefSetupParams(RwaObjDef *def, RwaInputParamDef *inputs, RwUInt32 nInputs,
                                       RwaOutputParamDef *outputs, RwUInt32 nOutputs);
extern "C" RwaObjDef *RwaObjDefDestroy(RwaObjDef *def);
extern "C" void *func_002934C0(RwUInt32 size, RwUInt32 hint);
extern "C" void func_00293440(void *p);

// 0x002ACD10
extern "C" void _RwaWaveRendererStateDestroyNULLPacket(RwUInt8 *st)
{
}

// The NULL packet (+0x78): the global NULL packet's data (D_01E81004, D_01E81008 bytes) for every feed; ee: the
// feeds' waves are EE-side (flag 0x20).
// 0x002ACD20
extern "C" void _RwaWaveRendererStateCreateNULLPacket(RwUInt8 *st, RwBool ee)
{
    if (func_002AB160_inl1(st, 0x34) != D_01E81008)
        printf(D_004C1830);
    func_002AB160_inl1(st, 0x78) = 0;
    func_002AB160_inl1(st, 0x80) = D_01E81008;
    func_002AB160_inl1(st, 0x88) = 0;
    func_002AB160_inl1(st, 0x84) = func_002AB160_inl1(st, 0x40);
    func_002AB160_inl1(st, 0x7C) = D_01E81004;
    if (ee)
        func_002AB160_inl1(st, 0x8C) |= 0x20;
}

// 0x002ACDB0
extern "C" void _RwaWaveRendererStateDestroyTrackingBuffer(RwUInt8 *st)
{
    func_00293440((void *)func_002AB160_inl1(st, 0x40));
}

// 0x002ACDC0
extern "C" void *_RwaWaveRendererStateCreateTrackingBuffer(RwUInt8 *st)
{
    void *p = func_002934C0(func_002AB160_inl1(st, 0x3C) * 24, 0x30809);
    func_002AB160_inl1(st, 0x40) = (unsigned int)p;
    return p;
}

// A feed voice's destruction: the caller's free function {fn, arg, voice} when given; else an SPU2 voice (its
// definition) is freed by its definition, and any other is only filled with 0xCD.
// 0x002ACF60
extern "C" void _RwaWaveRendererDestroyVoiceCallback(RwaObj *obj, RwUInt32 *ctx)
{
    if (ctx[0]) {
        (*(UnkFn_002ACF60 *)&ctx[0])(obj, ctx[1]);
        return;
    }
    RwaObjDef *def = obj->definition;
    if (def != _rwaObjDefFindByUUID(0, &rwaSPU2VOICEID, 0))
        _rwaObjDefFreeObj(obj, *(UnkFn_002ACF60 *)&ctx[0], ctx[1]);
    else
        memset((void *)ctx[2], 0xCD, 0xD0);
}

typedef RwUInt32 (*UnkFn_002AD000)(RwaObjDef *def, RwUInt32 arg, RwUInt32 p2, RwUInt32 p3);

// A feed voice's memory: the feed's own voice slot (ctx[2]) for an SPU2 voice, or the caller's allocator {fn, arg}.
// 0x002AD000
extern "C" RwUInt32 _RwaWaveRendererCreateVoiceCallback(RwaObjDef *def, RwUInt32 *ctx, RwUInt32 p2, RwUInt32 p3)
{
    RwUInt32 r = 0;
    if (def == _rwaObjDefFindByUUID(0, &rwaSPU2VOICEID, 0))
        r = ctx[2];
    if (ctx[0]) {
        RwUInt32 v = (*(UnkFn_002AD000 *)&ctx[0])(def, ctx[1], p2, p3);
        if (v)
            r = v;
    }
    return r;
}

// The global NULL packet, once: `size` bytes of IOP memory, announced to the IOP with opcode 0x49 (p6 address, p7
// size).
// 0x002AD0B0
extern "C" void _RwaWaveRendererSubCreateGlobalNULLPacket(RwUInt32 size)
{
    if (D_01E81004)
        return;
    D_01E81008 = size;
    D_01E81004 = sceSifAllocSysMemory(0, size, 0);
    RwaRPCTransfer(0, 0, 0, 0x49, 0, 0, D_01E81004, D_01E81008);
}

// 0x002AB790
extern "C" RwInt32 _RwaWaveRendererDestroy(RwaObj *obj, RwUInt32 p1, RwUInt32 p2)
{
    RwUInt8 *st = (RwUInt8 *)func_002AB160_inl1(obj, 0x20);
    RwBool ee = (func_002AB160_inl1(st, 0x8C) & 0x20) != 0;
    if (func_002AB160_inl1(st, 0x38)) {
        func_002AB160_inl3(func_002AB160_inl1(st, 0x38));
        func_002AB160_inl1(st, 0x38) = 0;
    }
    _RwaWaveRendererStateInitialise(st);
    _RwaWaveRendererStop(obj);
    _RwaWaveRendererDestroyAudioFeeds(obj, p1, p2);
    _RwaWaveRendererStateDestroyTrackingBuffer(st);
    if (ee)
        func_002AB160_inl1(st, 0x8C) |= 0x20;
    _RwaWaveRendererStateDestroyNULLPacket(st);
    func_002AB160_inl1(st, 0x8C) &= ~0x20u;
    RwaObjInterfaceRemove((RwaObjInterface *)func_002AB160_inl1(st, 0x30));
    return _RwaStreamNodeDestroy(obj);   // [C] retail also passes a1 / a2 through; the callee ignores them
}

// Creation parameters (words): [3] ring blocks, [5] / [6] interface attach arguments, [7] the format (+0x20 block
// bytes), [0xB] the shared renderer list, [0xC] EE-side waves; the renderer joins the list at its head.
// 0x002AB880
extern "C" RwaObj *_RwaWaveRendererCreate(RwaObj *obj, RwaObj *p1, RwUInt32 *params, RwUInt32 p3, RwUInt32 p4)
{
    RwUInt8 *st = (RwUInt8 *)func_002AB160_inl1(obj, 0x20);
    if (!_RwaStreamNodeCreate(obj, p1, params))
        return 0;
    func_002AB160_inl1(st, 0x30) = (unsigned int)RwaObjInterfaceAttach(
        RwaObjInterfaceDefFindByUUID(&rwaSTREAMRENDERERINTERFACEID), obj->definition, params[5], params[6]);
    func_002AB160_inl1(st, 0x34) = func_002AB160_inl1((void *)params[7], 0x20);
    func_002AB160_inl1(st, 0x38) = 0;
    func_002AB160_inl1(st, 0x3C) = params[3];
    _RwaWaveRendererStateCreateTrackingBuffer(st);
    _RwaWaveRendererStateInitialise(st);
    RwUInt32 *list = (RwUInt32 *)params[0xB];
    func_002AB160_inl1(st, 0xB4) = list[0];
    func_002AB160_inl1(st, 0xB8) = params[0xB];
    ((RwUInt32 *)((RwUInt32 *)params[0xB])[0])[1] = (unsigned int)(st + 0xB4);
    ((RwUInt32 *)params[0xB])[0] = (unsigned int)(st + 0xB4);
    func_002AB160_inl1(st, 0xB0) = params[0xB];
    if (!_RwaWaveRendererCreateAudioFeeds(obj, p1, params, p3, p4)) {
        _RwaWaveRendererStateDestroyTrackingBuffer(st);
        _RwaWaveRendererStateDestroyNULLPacket(st);
        _RwaStreamNodeDestroy(obj);   // [C] retail also passes a1 / a2 through; the callee ignores them
        return 0;
    }
    _RwaWaveRendererStateCreateNULLPacket(st, params[0xC]);
    RwaObjIntSetParamUInt32(obj, (RwaObjInterface *)func_002AB160_inl1(obj, 0x40), 0, 1);
    _RwaStreamNodeCreationComplete(obj);
    return obj;
}


extern "C" RwaObj *_rwaStreamNodeObjSetOutputConnection(RwaObj *obj, RwUInt32 param, RwUInt32 *conn);

// 0x002AB210
extern "C" RwaObj *_RwaWaveRendererSetOutputConnection(RwaObj *obj, RwUInt32 param, RwUInt32 *conn)
{
    _rwaStreamNodeObjSetOutputConnection(obj, 0, conn);
    return obj;
}

// Getter: the gain.
// 0x002AB380
extern "C" RwaObj *func_002AB380(RwaObj *obj, RwUInt32 param, RwReal *out)
{
    *out = *(RwReal *)((RwUInt8 *)func_002AB160_inl1(obj, 0x20) + 0xAC);
    return obj;
}

// Getter: the first feed's frequency, truncated.
// 0x002AB400
extern "C" RwaObj *func_002AB400(RwaObj *obj, RwUInt32 param, RwUInt32 *out)
{
    RwUInt8 *f = (RwUInt8 *)func_002AB160_inl1((void *)func_002AB160_inl1(obj, 0x20), 0x98);
    RwReal v = *(RwReal *)(f + 0xF8);
    *out = (int)v;
    return obj;
}

// Start execute: nothing.
// 0x002AB4F0
extern "C" RwaObj *func_002AB4F0(RwaObj *obj)
{
    return obj;
}

// 0x002AB9E0
extern "C" RwaObjDef *func_002AB9E0(void)
{
    return RwaObjDefDestroy(&D_01E80FA0);
}

// 0x002AB9F0
extern "C" void _RwaWaveRendererRegisterCommon(RwaObjDef *def)
{
    RwaObjDefAssignID(def, &rwaWAVERENDERERID, 0);
    func_00297C30_inl3(def, 0x100, 0x40);
    def->createFunc = _RwaWaveRendererCreate;
    def->destroyFunc = _RwaWaveRendererDestroy;
    def->startExecuteFunc = func_002AB4F0;
    def->finishExecuteFunc = _RwaWaveRendererFinishExecute;
    def->getParamsFunc = _RwaWaveRendererGetParams;
    def->createParamSize = 0x38;
    def->miscData = _rendererMiscData;
    def->noFreeListEntries = 2;
}

// 0x002ABCE0
extern "C" RwaObjDef *RwaWaveRendererRegister(void)
{
    RwaObjDef *parent = _rwaObjDefFindByUUID(0, &rwaSPU2OUTPUTID, 0);
    RwaObjDef *def = RwaObjDefCreate(parent, 0xC, &D_01E80FA0, &D_01E80F80, &_waverendererAliasInfo, 0x3080F);
    if (!def)
        return 0;
    _RwaWaveRendererRegisterCommon(def);
    if (RwaObjDefSetupParams(def, _waverendererInputParams, 5, _waverendererOutputParams, 6))
        return def;
    RwaObjDefDestroy(def);
    return 0;
}

typedef void (*UnkFn_002ABDFC)(RwaObj *obj, RwUInt32 *ctx);
typedef RwUInt32 (*UnkFn_002AC070)(RwaObjDef *def, RwUInt32 *ctx, RwUInt32 p2, RwUInt32 p3);
// Called with the address of a local block (as retail passes its stack): a word (RwaObjDestroy's definition takes RwUInt32).
extern "C" void RwaObjDestroy(RwaObj *obj, UnkFn_002ABDFC fn, RwUInt32 ctx);
extern "C" RwInt32 RwaWaveDefFormatWaveFormatCompare(void *defFormat, RwUInt8 *format);
extern "C" RwUInt32 RwaWaveFormatGetSamples(RwUInt8 *format, RwUInt32 bytes);
extern "C" RwaWave *RwaSPU2WaveCreate(RwUInt32 p0, RwUInt32 format, RwUInt32 target, RwUInt32 p3, RwUInt32 wave);
extern "C" RwaObj *RwaSPU2VoiceCreate(RwUInt32 p0, RwUInt32 p1, RwUInt32 fixed, UnkFn_002AC070 cb, RwUInt32 ctx);
extern "C" RwUInt32 _rwaObjFindFixed(RwaObjDef *def);
extern "C" void _rwaSPU2VoiceSetWave(RwUInt8 *v, RwUInt8 *wave);       // RwaSPU2Voice, RwaWave
extern "C" void _rwaSPU2VoiceSetPan(RwUInt8 *v, RwReal pan);
extern "C" void RwaSPU2WaveDestroy(RwUInt8 *wave);

// Each feed: its voice stopped (state 1) and unhooked, destroyed through _RwaWaveRendererDestroyVoiceCallback with
// {p1, p2, voice}, its wave destroyed; then the feed array freed.
// 0x002ABD90
extern "C" void _RwaWaveRendererDestroyAudioFeeds(RwaObj *obj, RwUInt32 p1, RwUInt32 p2)
{
    B3_RETAIL_FRAME(0x50);   // [C] frame 0x50; the destroy context {p1, p2, voice} at sp+0x40 is passed to RwaObjDestroy as a word
    RwUInt8 *st = (RwUInt8 *)func_002AB160_inl1(obj, 0x20);
    RwUInt32 *ctx = B3_FRAME_LOCAL(RwUInt32, 0x40);
    ctx[1] = p2;
    ctx[0] = p1;
    for (RwUInt32 i = 0; i < func_002AB160_inl1(st, 0x9C); i++) {
        RwUInt8 *f = (RwUInt8 *)(func_002AB160_inl1(st, 0x98) + i * 0x130);
        *(RwUInt32 *)func_002AB160_inl1(f, 0x90) = 1;
        _rwaSPU2VoiceSetWave(f + 0x60, 0);
        ctx[2] = (unsigned int)(f + 0x60);
        RwaObjDestroy((RwaObj *)(f + 0x60), (UnkFn_002ABDFC)_RwaWaveRendererDestroyVoiceCallback, (RwUInt32)ctx);
        RwaSPU2WaveDestroy(f);
    }
    func_00293440((void *)func_002AB160_inl1(st, 0x98));
    func_002AB160_inl1(st, 0x98) = 0;
}

// The feeds for the stream format (params word 7; +0x1C == 1: one mono feed per channel, else one feed): a
// renderer wave format matching it (fewer channels tried down to 1; retail does not restart the format list
// between tries [C 0x2ABF04]), per feed a sound-RAM wave of ring blocks x bytes per block and an SPU2 voice placed in
// the feed (stereo panned -1 / +1, mono centred). Returns the feed count, 0 on failure.
// 0x002ABE50
extern "C" RwUInt32 _RwaWaveRendererCreateAudioFeeds(RwaObj *obj, RwaObj *p1, RwUInt32 *params, RwUInt32 p3, RwUInt32 p4)
{
    RwUInt8 *fmt = (RwUInt8 *)params[7];
    B3_RETAIL_FRAME(0xD0);   // [C] frame 0xD0; the wave format copy at sp+0xA0 and the voice context at sp+0xC0 are passed as words
    RwUInt8 *st = (RwUInt8 *)func_002AB160_inl1(obj, 0x20);
    RwUInt8 *wf = B3_FRAME_LOCAL(RwUInt8, 0xA0);
    memcpy(wf, (void *)func_002AB160_inl1(fmt, 0x14), 0x1C);
    RwUInt8 *misc = (RwUInt8 *)_rwaObjDefFindByUUID(0, &rwaWAVERENDERERID, 0)->miscData;
    RwUInt32 *list = (RwUInt32 *)func_002AB160_inl1(misc, 4);
    RwBool found = 0;
    for (;;) {
        for (RwUInt32 i = 0; i < func_002AB160_inl1(misc, 8); i++, list++) {
            if (RwaWaveDefFormatWaveFormatCompare((void *)*list, wf)) {
                found = 1;
                break;
            }
        }
        if (found)
            break;
        wf[0xD] = wf[0xD] - 1;
        if (!((RwInt32)wf[0xD] > 0))
            break;
    }
    if (!found)
        return 0;
    RwUInt32 n = 1;
    if (*(RwUInt16 *)(fmt + 0x1C) == 1) {
        wf[0xD] = 1;
        n = ((RwUInt8 *)func_002AB160_inl1(fmt, 0x14))[0xD];
    }
    func_002AB160_inl1(st, 0x98) = (unsigned int)func_002934C0(n * 0x130, 0x30809);
    RwaObj *fixed = (RwaObj *)_rwaObjFindFixed(_rwaObjDefFindByUUID(0, &rwaSPU2VOICEID, 0));
    RwUInt32 *ctx = B3_FRAME_LOCAL(RwUInt32, 0xC0);
    ctx[1] = p4;
    ctx[0] = p3;
    func_002AB160_inl1(st, 0xA0) = func_002AB160_inl1(st, 0x34) / n;
    wf[0x19] = 1;
    *(RwUInt32 *)(wf + 8) = func_002AB160_inl1(st, 0xA0) * func_002AB160_inl1(st, 0x3C);
    func_002AB160_inl1(st, 0x48) = ((RwUInt8 *)func_002AB160_inl1(fmt, 0x14))[0xD];
    func_002AB160_inl1(st, 0x50) = RwaWaveFormatGetSamples(wf, *(RwUInt32 *)(wf + 8));
    func_002AB160_inl1(st, 0x4C) = RwaWaveFormatGetSamples(wf, func_002AB160_inl1(st, 0x34));
    func_002AB160_inl1(st, 0xA4) = func_002AB160_inl1(st, 0x4C) / n;
    for (RwUInt32 i = 0; i < n; i++) {
        RwUInt8 *f = (RwUInt8 *)(func_002AB160_inl1(st, 0x98) + i * 0x130);
        if (!RwaSPU2WaveCreate((RwUInt32)p1, (RwUInt32)wf, (RwUInt32)wf, params[0xD], (RwUInt32)f))
            return 0;
        func_002AB160_inl1(f, 0x54) |= 0x20;
        if (params[0xC])
            func_002AB160_inl1(f, 0x54) |= 0x40;
        else
            func_002AB160_inl1(f, 0x54) &= ~0x40u;
        ctx[2] = (unsigned int)(f + 0x60);
        if (!RwaSPU2VoiceCreate((RwUInt32)p1, 0, (RwUInt32)fixed, (UnkFn_002AC070)_RwaWaveRendererCreateVoiceCallback, (RwUInt32)ctx))
            return 0;
        _rwaSPU2VoiceSetWave(f + 0x60, f);
        *(RwUInt16 *)(f + 0xBC) |= 0x100;
        func_002AB160_inl1(f, 0xFC) = 0;
    }
    if (n == 2) {
        _rwaSPU2VoiceSetPan((RwUInt8 *)(func_002AB160_inl1(st, 0x98) + 0x60), -1.0f);
        _rwaSPU2VoiceSetPan((RwUInt8 *)(func_002AB160_inl1(st, 0x98) + 0x190), 1.0f);
    } else if (n == 1) {
        _rwaSPU2VoiceSetPan((RwUInt8 *)(func_002AB160_inl1(st, 0x98) + 0x60), 0.0f);
    }
    func_002AB160_inl1(st, 0x9C) = n;
    return n;
}
