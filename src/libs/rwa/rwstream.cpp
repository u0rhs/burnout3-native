// rwstream.o (librwacore.a; truncated in the E map) -- RWA's asynchronous RwStream reader over the IOP file
// operations: one request active at a time (the others queued), with a one-sector (0x800-byte) cache for unaligned or
// short reads; chunk header reads and chunk searches on top. Retail 0x2A0F50-0x2A1BC4 [C E map]. C port written from
// retail's code (audio1); proven by the checker on live captures.
#include "rwa/rwacore.h"

typedef void (*UnkFn_002A1540)(RwUInt32 handle, RwUInt32 result, RwUInt32 status, RwUInt32 kind, RwUInt32 arg);
struct UnkStruct_002A1540;
// A read request (at reader + 4) [C _rwaRwStreamCacheReadAsync 0x2A1540, _rwaRwStreamCacheReadAsyncCall 0x2A0F50].
struct UnkStruct_002A1540_req {
    UnkStruct_002A1540_req *field_0;  // 0x00 queue link next (0 when not queued)
    UnkStruct_002A1540_req *field_4;  // 0x04 queue link prev
    RwUInt32 field_8;                 // 0x08 state: 1 idle / done, 2 queued, 3 active, 4 reading, 5 seeking
    RwUInt32 field_C;                 // 0x0C file position
    RwUInt32 field_10;                // 0x10 bytes requested
    RwUInt32 field_14;                // 0x14 bytes left
    RwUInt32 field_18;                // 0x18 EE destination
    RwUInt32 field_1C;                // 0x1C the IOP file handle
    RwUInt32 field_20;
    RwUInt32 field_24;                // 0x24 result of the open (kind 2)
    RwUInt32 field_28;                // 0x28 the sector read
    RwUInt32 field_2C;                // 0x2C its bytes
    RwUInt32 field_30;                // 0x30 its EE buffer (the destination or the sector cache)
    UnkStruct_002A1540 *field_34;     // 0x34 the reader
    UnkFn_002A1540 field_38;          // 0x38 completion callback
    RwUInt32 field_3C;                // 0x3C its argument
};
// The reader [C _rwaRwStreamOpen 0x2A1AC4-0x2A1B20].
struct UnkStruct_002A1540 {
    RwUInt32 *field_0;                // 0x00 the RwStream (+0 type, +0xC the file handle)
    UnkStruct_002A1540_req field_4;   // 0x04
    RwUInt32 field_44;                // 0x44 the pending operation: 1 open, 2 find chunk, 3 read / seek
    UnkFn_002A1540 field_48;          // 0x48 the caller's callback
    RwUInt32 field_4C;                // 0x4C its argument
    RwUInt32 field_50;                // 0x50 the chunk type searched for
    RwUInt32 *field_54;               // 0x54 the chunk header (type, size, version)
    RwUInt32 field_58;                // 0x58 1 while skipping a chunk
    RwUInt32 field_5C;                // 0x5C the IOP file handle
    RwUInt32 field_60;                // 0x60 the IOP file position
};
struct UnkStruct_01E7D568 {
    UnkStruct_002A1540_req *field_0;
    UnkStruct_002A1540_req *field_4;
};
typedef RwInt32 (*UnkFn_002A1810)(RwUInt32 file);

extern RwInt32 _rwaGprogramContext;
extern RwInt32 D_01E7D560;                    // held while a request is active
extern RwInt32 D_01E7D564;                    // the queue's semaphore
extern UnkStruct_01E7D568 D_01E7D568;         // the queue head
extern UnkStruct_002A1540_req *D_01E7D570;    // the active request
extern RwUInt8 D_01E7D580[0x800];             // the sector cache
extern UnkStruct_002A1540_req *D_01E7DD80;    // the request the cache was filled for
extern RwUInt32 D_01E7DD84;                   // its file handle
extern RwUInt32 D_01E7DD88;                   // the cached sector (-1 none)
extern RwUInt32 D_01E7DD8C;                   // bytes in the cache
extern RwInt32 D_01E7DD90;                    // the reader's semaphore
extern "C" void *memcpy(void *dst, const void *src, unsigned int n);
extern "C" RwInt32 WaitSema(RwInt32 id);
extern "C" RwInt32 SignalSema(RwInt32 id);
extern "C" RwBool _rwaSemaphorePoll(RwInt32 *sema);
extern "C" RwInt32 *RwaSemaphoreCreate(RwInt32 *sema, RwInt32 init, RwInt32 max);
extern "C" RwUInt8 *func_001D8B88(void);      // RwOsGetFileInterface [E map]
extern "C" void RwaSPU2RemoteIOPFopsSeek(RwUInt32 handle, RwUInt32 pos, UnkFn_002A1540 cb, RwUInt32 arg);
extern "C" void RwaSPU2RemoteIOPFopsReadToEE(RwUInt32 handle, RwUInt32 buffer, RwUInt32 size, RwUInt32 *done,
                                             UnkFn_002A1540 cb, RwUInt32 arg);
extern "C" RwUInt32 _rwaSPU2RemoteIOPFopsHasIOPFileBeenOpened(RwUInt32 handle);
extern "C" void RwaSPU2RemoteIOPFopsClose(RwUInt32 handle);
extern "C" RwUInt32 RwaSPU2RemoteIOPFopsRegisterForOpen(const char *name);

static void func_002A0F50_inl1(RwInt32 *sema)
{
    if (_rwaGprogramContext == 2)
        WaitSema(*sema);
}

static void func_002A0F50_inl2(RwInt32 *sema)
{
    if (_rwaGprogramContext == 2)
        SignalSema(*sema);
}

// The reader's state machine, also the IOP completion callback (kind 1 start, 2 open, 4 seek, 5 read). A request is
// served from the sector cache where it covers it, by direct sector-aligned reads (destination 64-byte aligned, at
// least a sector), else a sector at a time through the cache; a seek precedes any read away from the file position.
// When a request is done its callback runs and the next queued request starts (the cache's own first).
// 0x002A0F50
extern "C" void _rwaRwStreamCacheReadAsyncCall(RwUInt32 handle, RwUInt32 result, RwUInt32 status, RwUInt32 kind,
                                               UnkStruct_002A1540 *o)
{
    UnkStruct_002A1540_req *r = &o->field_4;
    if (kind != 5 && kind != 1) {
        if (kind == 2)
            r->field_24 = result;
        if (r->field_8 != 5) {
            r->field_38(handle, result, status, kind, r->field_3C);
            return;
        }
        r->field_8 = 3;
    }
    func_002A0F50_inl1(&D_01E7DD90);
    do {
        if (r->field_8 == 4) {
            r->field_8 = 3;
            if (status == 4 || result != r->field_2C)
                goto done;
            if (r->field_30 == (unsigned int)D_01E7D580) {
                D_01E7DD8C = result;
                D_01E7DD80 = r;
                D_01E7DD84 = handle;
                D_01E7DD88 = r->field_28;
            } else {
                r->field_18 += result;
                r->field_C += result;
                r->field_14 -= result;
            }
        }
        {
            RwUInt32 pos = r->field_C;
            RwUInt32 sector = pos & 0xFFFFF800;
            if (D_01E7DD80 == r && D_01E7DD88 == sector) {
                RwUInt32 off = pos - D_01E7DD88;
                RwUInt32 n = r->field_14;
                if (D_01E7DD8C - off < n)
                    n = D_01E7DD8C - off;
                memcpy((void *)r->field_18, D_01E7D580 + off, n);
                pos += n;
                status = 2;
                sector = pos & 0xFFFFF800;
                r->field_18 += n;
                r->field_14 -= n;
                r->field_C += n;
            }
            RwUInt32 left = r->field_14;
            if (!left)
                goto done;
            RwUInt32 bytes;
            if ((r->field_18 & 0x3F) || pos != sector || left < 0x800) {
                D_01E7DD80 = 0;
                D_01E7DD84 = 0;
                bytes = 0x800;
                D_01E7DD88 = 0xFFFFFFFF;
                r->field_30 = (unsigned int)D_01E7D580;
            } else {
                r->field_30 = r->field_18;
                bytes = left & 0xFFFFF800;
                if (r == D_01E7DD80) {
                    D_01E7DD80 = 0;
                    D_01E7DD84 = 0;
                }
            }
            if (o->field_60 != sector) {
                r->field_8 = 5;
                o->field_60 = sector;
                RwaSPU2RemoteIOPFopsSeek(o->field_5C, sector, (UnkFn_002A1540)_rwaRwStreamCacheReadAsyncCall,
                                         (unsigned int)o);
                break;
            }
            r->field_8 = 4;
            r->field_28 = sector;
            r->field_2C = bytes;
            o->field_60 += r->field_2C;
            RwaSPU2RemoteIOPFopsReadToEE(r->field_1C, r->field_30, r->field_2C, 0,
                                         (UnkFn_002A1540)_rwaRwStreamCacheReadAsyncCall, (unsigned int)o);
            break;
        }
    done:
        r->field_8 = 1;
        r->field_38(r->field_1C, r->field_10, status, kind, r->field_3C);
        func_002A0F50_inl1(&D_01E7D564);
        r = 0;
        if (D_01E7D568.field_0 == (UnkStruct_002A1540_req *)&D_01E7D568) {
            func_002A0F50_inl2(&D_01E7D560);
        } else {
            UnkStruct_002A1540_req *n = D_01E7DD80;
            if (!n || !n->field_0)
                n = D_01E7D568.field_0;
            r = n;
            n->field_4->field_0 = n->field_0;
            n->field_0->field_4 = n->field_4;
            n->field_0 = 0;
            r->field_8 = 3;
            o = r->field_34;
        }
        func_002A0F50_inl2(&D_01E7D564);
        D_01E7D570 = r;
    } while (r);
    func_002A0F50_inl2(&D_01E7DD90);
}

// Cancels the reader's request: the active one is cut short (its callback runs, status 2); a queued one leaves the
// queue (state 1).
// 0x002A13B0
extern "C" void _rwaRwStreamCacheReadCancel(UnkStruct_002A1540 *o)
{
    UnkStruct_002A1540_req *r = &o->field_4;
    RwBool active = 0;
    func_002A0F50_inl1(&D_01E7DD90);
    func_002A0F50_inl1(&D_01E7D564);
    if (_rwaSemaphorePoll(&D_01E7D560)) {
        func_002A0F50_inl2(&D_01E7D560);
    } else {
        if (D_01E7D570 == r) {
            r->field_8 = 3;
            active = 1;
            r->field_14 = 0;
        }
        if (D_01E7DD80 == r) {
            D_01E7DD80 = 0;
            D_01E7DD84 = 0;
        }
    }
    if (r->field_0) {
        r->field_4->field_0 = r->field_0;
        r->field_0->field_4 = r->field_4;
        r->field_0 = 0;
        r->field_8 = 1;
    }
    func_002A0F50_inl2(&D_01E7D564);
    func_002A0F50_inl2(&D_01E7DD90);
    if (active)
        _rwaRwStreamCacheReadAsyncCall(r->field_1C, 0, 2, 1, o);
}

// Reads size bytes at the stream position into buf; starts at once when no request is active, else queues.
// 0x002A1540
extern "C" void _rwaRwStreamCacheReadAsync(UnkStruct_002A1540 *o, RwUInt32 buf, RwUInt32 size, UnkFn_002A1540 cb,
                                           RwUInt32 arg)
{
    UnkStruct_002A1540_req *r = &o->field_4;
    o->field_4.field_8 = 3;
    o->field_4.field_1C = o->field_0[3];
    o->field_4.field_10 = size;
    o->field_4.field_14 = size;
    o->field_4.field_18 = buf;
    o->field_4.field_38 = cb;
    o->field_4.field_3C = arg;
    func_002A0F50_inl1(&D_01E7D564);
    if (_rwaSemaphorePoll(&D_01E7D560)) {
        D_01E7D570 = r;
        func_002A0F50_inl2(&D_01E7D564);
        _rwaRwStreamCacheReadAsyncCall(r->field_1C, r->field_10, 0, 1, o);
        return;
    }
    r->field_0 = D_01E7D568.field_0;
    r->field_4 = (UnkStruct_002A1540_req *)&D_01E7D568;
    D_01E7D568.field_0->field_4 = r;
    D_01E7D568.field_0 = r;
    r->field_8 = 2;
    func_002A0F50_inl2(&D_01E7D564);
}

// 0x002A16F0
extern "C" RwBool _rwaRwStreamCacheOpenModule(void)
{
    D_01E7DD80 = 0;
    D_01E7DD84 = 0;
    D_01E7DD88 = 0xFFFFFFFF;
    D_01E7DD8C = 0;
    RwaSemaphoreCreate(&D_01E7DD90, 1, 1);
    D_01E7D570 = 0;
    D_01E7D568.field_0 = (UnkStruct_002A1540_req *)&D_01E7D568;
    D_01E7D568.field_4 = (UnkStruct_002A1540_req *)&D_01E7D568;
    RwaSemaphoreCreate(&D_01E7D564, 1, 1);
    RwaSemaphoreCreate(&D_01E7D560, 1, 1);
    return 1;
}

// 0x002A1790
extern "C" RwUInt32 _rwaRwStreamGetPos(UnkStruct_002A1540 *o)
{
    return o->field_4.field_C;
}

extern "C" void _RwaRwStreamFileOpCallback(RwUInt32 handle, RwUInt32 result, RwUInt32 status, RwUInt32 kind,
                                           UnkStruct_002A1540 *o);

// 0x002A17A0
extern "C" void _rwaRwStreamReadChunkHeader(UnkStruct_002A1540 *o, RwUInt32 *header, UnkFn_002A1540 cb, RwUInt32 arg)
{
    o->field_48 = cb;
    o->field_4C = arg;
    o->field_44 = 3;
    _rwaRwStreamCacheReadAsync(o, (unsigned int)header, 0xC, (UnkFn_002A1540)_RwaRwStreamFileOpCallback,
                               (unsigned int)o);
}

// Reads chunk headers, skipping chunks, until one of `type`.
// 0x002A17D0
extern "C" void _rwaRwStreamFindChunk(RwUInt32 *stream, UnkStruct_002A1540 *o, RwUInt32 type, RwUInt32 *header,
                                      UnkFn_002A1540 cb, RwUInt32 arg)
{
    o->field_0 = stream;
    o->field_50 = type;
    o->field_54 = header;
    o->field_58 = 0;
    o->field_48 = cb;
    o->field_4C = arg;
    o->field_44 = 2;
    _rwaRwStreamCacheReadAsync(o, (unsigned int)header, 0xC, (UnkFn_002A1540)_RwaRwStreamFileOpCallback,
                               (unsigned int)o);
}

// A chunk header has arrived (or a skip seek finished): the caller's callback gets (file, 0xC) when it is the chunk
// searched for, (file, 0, 4) on an error (the header cleared); otherwise the chunk is skipped (a seek past its size
// from the file's position) and the next header read.
// 0x002A1810
extern "C" void _RwaRwStreamFindChunkCallback(RwUInt32 handle, RwUInt32 result, RwUInt32 status, RwUInt32 kind,
                                              UnkStruct_002A1540 *o)
{
    if (status == 4) {
        o->field_44 = 0;
        o->field_54[0] = 0;
        o->field_54[1] = 0;
        o->field_54[2] = 0;
        o->field_48(o->field_0[3], 0, status, kind, o->field_4C);
        return;
    }
    if (o->field_58) {
        o->field_58 = 0;
        _rwaRwStreamCacheReadAsync(o, (unsigned int)o->field_54, 0xC, (UnkFn_002A1540)_RwaRwStreamFileOpCallback,
                                   (unsigned int)o);
        return;
    }
    if (o->field_54[0] == o->field_50) {
        o->field_44 = 0;
        o->field_48(o->field_0[3], 0xC, status, kind, o->field_4C);
        return;
    }
    o->field_58 = 1;
    RwUInt8 *fi = func_001D8B88();
    RwInt32 p = (*(UnkFn_002A1810 *)(fi + 0x28))(o->field_0[3]);
    if (p < 0) {
        o->field_54[0] = 0;
        o->field_54[1] = 0;
        o->field_54[2] = 0;
        o->field_48(o->field_0[3], 0, 4, kind, o->field_4C);
        return;
    }
    o->field_60 = p + o->field_54[1];
    RwaSPU2RemoteIOPFopsSeek(o->field_5C, p + o->field_54[1], (UnkFn_002A1540)_rwaRwStreamCacheReadAsyncCall,
                             (unsigned int)o);
}

// Seek: the stream position, and an IOP seek to its sector.
// 0x002A1980
extern "C" void func_002A1980(UnkStruct_002A1540 *o, RwUInt32 pos, UnkFn_002A1540 cb, RwUInt32 arg)
{
    o->field_48 = cb;
    o->field_4C = arg;
    o->field_44 = 3;
    o->field_4.field_C = pos;
    o->field_60 = pos & 0xFFFFF800;
    RwaSPU2RemoteIOPFopsSeek(o->field_5C, pos & 0xFFFFF800, (UnkFn_002A1540)_rwaRwStreamCacheReadAsyncCall,
                             (unsigned int)o);
}

// Read with the caller's callback called directly.
// 0x002A19C0
extern "C" void func_002A19C0(UnkStruct_002A1540 *o, RwUInt32 buf, RwUInt32 size, UnkFn_002A1540 cb, RwUInt32 arg)
{
    o->field_48 = cb;
    o->field_4C = arg;
    o->field_44 = 3;
    _rwaRwStreamCacheReadAsync(o, buf, size, cb, arg);
}

// 0x002A19E0
extern "C" void func_002A19E0(UnkStruct_002A1540 *o)
{
    _rwaRwStreamCacheReadCancel(o);
}

// Close: cancel, drop the cache if it is this reader's, close the IOP file of a file-name stream.
// 0x002A19F0
extern "C" void func_002A19F0(UnkStruct_002A1540 *o)
{
    _rwaRwStreamCacheReadCancel(o);
    if (D_01E7DD80 == &o->field_4) {
        D_01E7DD80 = 0;
        D_01E7DD84 = 0;
        D_01E7DD88 = 0xFFFFFFFF;
        D_01E7DD8C = 0;
    }
    if (o->field_0[0] == 2 && _rwaSPU2RemoteIOPFopsHasIOPFileBeenOpened(o->field_5C)) {
        RwaSPU2RemoteIOPFopsClose(o->field_5C);
        o->field_5C = 0;
    }
}

// Opens a stream; for a file name (type 2) the reader registers the IOP file and the open completes through the
// callback (the stream's file handle set first).
// 0x002A1A90
extern "C" RwUInt32 *_rwaRwStreamOpen(RwUInt32 *stream, UnkStruct_002A1540 *o, RwUInt32 type, RwUInt32 access,
                                      const char *name, UnkFn_002A1540 cb, RwUInt32 arg)
{
    stream[2] = 0;
    stream[0] = type;
    stream[1] = access;
    if (type != 2)
        return stream;
    o->field_4.field_4 = 0;
    o->field_4.field_0 = 0;
    o->field_4.field_8 = 1;
    o->field_4.field_38 = (UnkFn_002A1540)_RwaRwStreamFileOpCallback;
    o->field_4.field_3C = (unsigned int)o;
    o->field_4.field_34 = o;
    o->field_4.field_C = 0;
    o->field_4.field_20 = 0;
    o->field_0 = stream;
    o->field_48 = cb;
    o->field_4C = arg;
    o->field_44 = 1;
    o->field_5C = RwaSPU2RemoteIOPFopsRegisterForOpen(name);
    o->field_60 = 0;
    _rwaRwStreamCacheReadAsyncCall(o->field_5C, 0, 2, 2, o);
    return stream;
}

// 0x002A1B50
extern "C" void _RwaRwStreamFileOpCallback(RwUInt32 handle, RwUInt32 result, RwUInt32 status, RwUInt32 kind,
                                           UnkStruct_002A1540 *o)
{
    switch (o->field_44) {
    case 3:
        o->field_48(handle, result, status, kind, o->field_4C);
        break;
    case 2:
        _RwaRwStreamFindChunkCallback(handle, result, status, kind, o);
        break;
    case 1:
        o->field_0[3] = handle;
        o->field_48(handle, result, status, kind, o->field_4C);
        break;
    }
}

extern "C" RwInt32 DeleteSema(RwInt32 id);

// Close: takes the reader's semaphore (context 2) and deletes it, then takes the queue and active-request semaphores
// (each only in context 2, _rwaGprogramContext re-read before each) and deletes both.
// 0x002A1650
extern "C" void _rwaRwStreamCacheCloseModule(void)
{
    if (_rwaGprogramContext == 2)
        WaitSema(D_01E7DD90);
    DeleteSema(D_01E7DD90);
    if (_rwaGprogramContext == 2)
        WaitSema(D_01E7D564);
    if (_rwaGprogramContext == 2)
        WaitSema(D_01E7D560);
    DeleteSema(D_01E7D564);
    DeleteSema(D_01E7D560);
}
