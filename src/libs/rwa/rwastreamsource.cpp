// rwastreamsource.o (librwacore.a) -- the stream source node: reads a stream file (.RWS chunks 0x80D / 0x80E / 0x80F)
// into a ring of blocks through the file request queue and hands locked blocks downstream as packets (parts). Retail
// 0x2A72D0-0x2AD1C0 [C E map]. C port written from retail's code (audio1); proven by the checker on live captures and
// synthetic variants.
#include "rwa/rwacore.h"

typedef void (*UnkFn_002A77B0)(RwUInt32 *pk, RwaObj *obj);
extern "C" void _RwaSourceStreamPacketCompletionCallback(RwUInt32 *pk, RwaObj *obj);
extern "C" RwaObj *_RwaStreamNodeGetParams(RwaObj *obj, RwUInt32 *params);
extern "C" void __rwaStreamSourceCreationParamsInit(RwUInt32 *params);
extern "C" void RwaStreamPacketQueueInitialize(void *queue, RwUInt32 packets, RwUInt32 n);
extern "C" void _RwaStreamNodeCreationComplete(void *node);

// A parameter getter: the word at +0x198.
// 0x002A7360
extern "C" RwaObj *func_002A7360(RwaObj *obj, RwUInt32 param, RwUInt32 *value)
{
    *value = *(RwUInt32 *)((char *)obj + 0x198);
    return obj;
}

// The source's creation parameters: the node's (with word 0 cleared), then the source defaults copied in
// [C 0x2A7858-0x2A78C8: the copy order below].
// 0x002A7830
extern "C" RwaObj *_RwaStreamSourceGetParams(RwaObj *obj, RwUInt32 *d)
{
    RwUInt32 t[0x14];
    _RwaStreamNodeGetParams(obj, d);
    d[0] = 0;
    __rwaStreamSourceCreationParamsInit(t);
    d[0x1C / 4] = t[0];
    d[0x34 / 4] = t[0x18 / 4];
    d[0x38 / 4] = t[0x1C / 4];
    d[0x24 / 4] = t[0x8 / 4];
    d[0x2C / 4] = t[0x10 / 4];
    d[0x30 / 4] = t[0x14 / 4];
    d[0x28 / 4] = t[0xC / 4];
    d[0x20 / 4] = t[0x4 / 4];
    d[0x58 / 4] = t[0x3C / 4];
    d[0x54 / 4] = t[0x38 / 4];
    d[0x3C / 4] = t[0x20 / 4];
    d[0x40 / 4] = t[0x24 / 4];
    d[0x48 / 4] = t[0x2C / 4];
    d[0x44 / 4] = t[0x28 / 4];
    return obj;
}

// Open: with `failed` set the node records the failure (+0x68 = 1); otherwise the packet completion callback {fn, node}
// at +0x230 and one entry (0x14 bytes, +0x224 array) per packet (0x10 bytes, +0x220 array; count +0x1E4), then the
// packet queue at +0x228. Either way the node's creation completes. No result: retail leaves v0 as the completion
// left it, which does not set it when there is no completion callback [C 0x2AD748-0x2AD760].
// 0x002A7AD0
extern "C" void _RwaStreamSourceOpenFunc(RwUInt8 *owner, RwUInt32 failed, RwUInt8 *node)
{
    if (failed) {
        *(RwUInt32 *)(node + 0x68) = 1;
        _RwaStreamNodeCreationComplete(node);
        return;
    }
    RwUInt32 pkts = *(RwUInt32 *)(node + 0x220);
    RwUInt32 n = *(RwUInt32 *)(node + 0x1E4);
    RwUInt32 entries = *(RwUInt32 *)(node + 0x224);
    *(UnkFn_002A77B0 *)(node + 0x230) = _RwaSourceStreamPacketCompletionCallback;
    *(RwUInt32 *)(node + 0x234) = (unsigned int)node;
    for (RwUInt32 i = 0; i < n; i++) {
        RwUInt32 *x = (RwUInt32 *)(pkts + i * 16);
        RwUInt32 *e = (RwUInt32 *)(entries + i * 20);
        e[0] = 0;
        e[1] = (unsigned int)node;
        e[4] = (unsigned int)(node + 0x230);
        RwUInt32 a = *(RwUInt32 *)(owner + 0x124);
        RwUInt32 b2 = *(RwUInt32 *)(a + 0x18);
        e[2] = *(RwUInt32 *)(b2 + 0x20);
        e[3] = (unsigned int)x;
        x[1] = 3;
        x[3] = 2;
    }
    RwaStreamPacketQueueInitialize(node + 0x228, entries, n);
    _RwaStreamNodeCreationComplete(node);
}

// The source node (state at obj + 0x40; offsets from obj): +0x60 process state, +0x68 status, +0x70 the node
// interface, +0x74 the source proper (+0x108 a pending request, +0x124 (obj + 0x198) the stream description, +0x154 /
// +0x158 / +0x160 the request ring, +0x178 / +0x184 the block list and current block, +0x1A0 flags: 0x8000 keep whole
// blocks, 0x20000 block-list mode), +0x228 the packet queue.
typedef RwUInt32 (*UnkFn_002A7310)(RwaObj *obj, RwUInt32 output);
extern "C" RwaObj *_rwaStreamNodeObjSetOutputConnection(RwaObj *obj, RwUInt32 param, RwUInt32 *conn);
extern "C" RwaObj *_rwaStreamNodeObjSetProcessState(RwaObj *obj, RwUInt32 param, RwUInt32 *value);
extern "C" RwInt32 _RwaStreamNodeDestroy(RwaObj *obj);   // [C] reads a0 only
extern "C" void _rwaStreamNodeProcessComplete(RwaObj *obj);
extern "C" RwUInt32 *RwaStreamPacketQueueAlloc(void *q);
extern "C" void RwaStreamPacketQueueFree(void *q);   // [C] reads a0 only (the packet in a1 is ignored)
extern "C" RwInt32 RwaObjInterfaceRemove(RwaObjInterface *intf);
extern "C" void __rwaStreamSourceFlush(RwUInt8 *src);
extern "C" void __rwaStreamSourceDestroy(RwUInt8 *src);
extern "C" RwUInt32 __rwaStreamSourceRead(RwUInt8 *src);
extern "C" void __rwaStreamSourceProcess(RwUInt8 *src);
extern "C" RwUInt32 __rwaStreamSourceLockStreamBlock(RwUInt8 *src, RwUInt32 *pos);
extern "C" void __rwaStreamSourceUnlockStreamBlock(RwUInt8 *src, RwUInt32 block);
extern "C" RwUInt32 __rwaStreamSourceGetSamplePosUsingPos(RwUInt8 *src, RwUInt32 *pos, RwUInt32 k);
extern RwInt32 _rwaGprogramContext;
extern "C" RwBool _rwaStreamSourceSetStreamPos(RwaObj *obj, RwUInt32 *pos);
extern "C" RwUInt32 RwaWaveFormatGetSamples(const void *format, RwUInt32 bytes);

// The word at p + off.
static RwUInt32 &func_002A7630_inl1(void *p, RwUInt32 off)
{
    return *(RwUInt32 *)((RwUInt8 *)p + off);
}

// 0x002A72D0
extern "C" RwaObj *_RwaStreamSourceSetFlush(RwaObj *obj, RwUInt32 param, RwBool *flush)
{
    if (*flush)
        __rwaStreamSourceFlush((RwUInt8 *)obj + 0x74);
    return obj;
}

extern "C" RwUInt32 _rwaStreamSourceGetStreamPacket(RwaObj *obj, RwUInt32 output);

// 0x002A7310
extern "C" RwaObj *_RwaStreamSourceSetOutputConnection(RwaObj *obj, RwUInt32 param, RwUInt32 *conn)
{
    _rwaStreamNodeObjSetOutputConnection(obj, 0, conn);
    *(UnkFn_002A7310 *)&((RwUInt32 *)conn[1])[1] = _rwaStreamSourceGetStreamPacket;
    ((RwUInt32 *)conn[1])[2] = (unsigned int)obj;
    return obj;
}

// Flag 0x8000 of the source (keep whole blocks).
// 0x002A7390
extern "C" RwaObj *func_002A7390(RwaObj *obj, RwUInt32 param, RwBool *on)
{
    if (*on)
        func_002A7630_inl1(obj, 0x214) |= 0x8000;
    else
        func_002A7630_inl1(obj, 0x214) &= 0xFFFF7FFF;
    return obj;
}

// 0x002A74A0
extern "C" RwaObj *_RwaStreamSourceSetStreamPos(RwaObj *obj, RwUInt32 param, RwUInt32 *pos)
{
    _rwaStreamSourceSetStreamPos(obj, pos);
    return obj;
}

// The next packet: a locked block of the stream (null when the queue is full or no block is ready), its segment index
// and sample position; for the stream's last block (no pending request, flag 0x8000 clear) the length is cut to the
// samples left (in format units, at most the format's block length) and the packet marked last (flag 8). The status
// test at the top never returns [C 0x2A7648: status == 3 and status != 8].
// 0x002A7630
extern "C" RwUInt32 _rwaStreamSourceGetStreamPacket(RwaObj *obj, RwUInt32 output)
{
    RwUInt8 *o = (RwUInt8 *)obj;
    if (o[0x229] == o[0x22B])
        return 0;
    RwUInt8 *s = o + 0x74;
    RwUInt32 pos;
    RwUInt32 blk = __rwaStreamSourceLockStreamBlock(s, &pos);
    if (!blk)
        return 0;
    RwUInt32 *pk = RwaStreamPacketQueueAlloc(o + 0x228);
    pk[1] = blk;
    RwUInt32 *x = (RwUInt32 *)pk[3];
    x[3] = 2;
    RwUInt8 *desc = (RwUInt8 *)func_002A7630_inl1(s, 0x124);
    ((RwUInt32 *)pk[3])[0] = (func_002A7630_inl1((void *)pos, 0) - func_002A7630_inl1(desc, 0x10)) >> 5;
    ((RwUInt32 *)pk[3])[2] = __rwaStreamSourceGetSamplePosUsingPos(s, (RwUInt32 *)pos, 0);
    if (func_002A7630_inl1(s, 0x108) != 0)
        return (unsigned int)pk;
    if (func_002A7630_inl1(s, 0x1A0) & 0x8000)
        return (unsigned int)pk;
    RwUInt8 *fmt = (RwUInt8 *)func_002A7630_inl1((void *)func_002A7630_inl1(s, 0x124), 0x18);
    RwUInt32 total = RwaWaveFormatGetSamples((const void *)func_002A7630_inl1(fmt, 0x14), func_002A7630_inl1((void *)func_002A7630_inl1(fmt, 0x14), 8));
    RwUInt32 per = RwaWaveFormatGetSamples((const void *)func_002A7630_inl1(fmt, 0x14), func_002A7630_inl1(fmt, 0x20));
    pk[2] = total - func_002A7630_inl1((void *)pos, 4) * per;
    RwUInt32 ch = ((RwUInt8 *)func_002A7630_inl1(fmt, 0x14))[0xD];
    pk[2] = (ch * (pk[2] * *(RwUInt16 *)(fmt + 0x1A))) / func_002A7630_inl1(fmt, 0xC);
    if (func_002A7630_inl1(fmt, 0x20) < pk[2])
        pk[2] = func_002A7630_inl1(fmt, 0x20);
    pk[0] |= 8;
    return (unsigned int)pk;
}

// A packet is done: a last packet stops the node (process state 1); its block is unlocked and the packet freed.
// 0x002A77B0
extern "C" void _RwaSourceStreamPacketCompletionCallback(RwUInt32 *pk, RwaObj *obj)
{
    if (pk[0] & 8) {
        pk[0] &= ~8u;
        RwUInt32 v = 1;
        _rwaStreamNodeObjSetProcessState(obj, 0, &v);
    }
    __rwaStreamSourceUnlockStreamBlock((RwUInt8 *)obj + 0x74, pk[1]);
    RwaStreamPacketQueueFree((RwUInt8 *)obj + 0x228);   // [C] retail also passes pk in a1; the callee ignores it
}

// Per update: stopped (1) completes the stop; running (2) settles the status (7 when the stream is at its end:
// the current block's flags 1 / 4, or the request ring drained to a final request; else 6); running or paused then
// reads (__rwaStreamSourceRead: 3 / 4 = read finished, 4 also stopping a running node) and processes.
// 0x002A78E0
extern "C" RwaObj *_RwaStreamSourceStartExecute(RwaObj *obj)
{
    RwUInt8 *s = (RwUInt8 *)obj + 0x74;
    RwUInt32 ps = func_002A7630_inl1(obj, 0x60);
    if (ps == 1) {
        if (func_002A7630_inl1(obj, 0x68) != 3)
            _rwaStreamNodeProcessComplete(obj);
        return obj;
    }
    if (ps == 2) {
        RwUInt32 st = func_002A7630_inl1(obj, 0x68);
        if (st != 6 && st != 7)
            _rwaStreamNodeProcessComplete(obj);
        RwBool end;
        if (func_002A7630_inl1(s, 0x1A0) & 0x20000) {
            RwUInt32 *e = (RwUInt32 *)(func_002A7630_inl1(s, 0x178) + 0x10 + func_002A7630_inl1(s, 0x184) * 20);
            end = (e[0] & 1) || (e[0] & 4);
        } else {
            end = 0;
            RwUInt32 cur = func_002A7630_inl1(s, 0x158);
            if (func_002A7630_inl1(s, 0x160) == cur &&
                func_002A7630_inl1((void *)(cur * 20 + func_002A7630_inl1(s, 0x154)), 0x10) == 1)
                end = 1;
        }
        func_002A7630_inl1(obj, 0x68) = end ? 7 : 6;
    } else if (ps != 3) {
        return obj;
    }
    RwUInt32 r = __rwaStreamSourceRead(s);
    if (r - 3 < 2) {
        if (func_002A7630_inl1(obj, 0x68) == 4)
            _rwaStreamNodeProcessComplete(obj);
        if (r == 4 && func_002A7630_inl1(obj, 0x60) == 2) {
            func_002A7630_inl1(obj, 0x60) = 1;
            _rwaStreamNodeProcessComplete(obj);
        }
    }
    __rwaStreamSourceProcess(s);
    return obj;
}

// 0x002A7BA0
extern "C" RwInt32 _RwaStreamSourceDestroy(RwaObj *obj, RwUInt32 p1, RwUInt32 p2)
{
    __rwaStreamSourceFlush((RwUInt8 *)obj + 0x74);
    RwaObjInterfaceRemove((RwaObjInterface *)func_002A7630_inl1(obj, 0x70));
    __rwaStreamSourceDestroy((RwUInt8 *)obj + 0x74);
    return _RwaStreamNodeDestroy(obj);   // [C] retail also passes a1 / a2 through; the callee ignores them
}

// The source proper (offsets from obj + 0x74) [C __rwaStreamSourceRead 0x2A87B0, Lock / Unlock 0x2A8BA0-0x2A8FA0]:
// +0x84 the IOP file, +0x98 the file request queue (+0xE4 the request being executed), +0xF8 the loop segment (-1
// none), +0xFC / +0x100 the current / requested value of entry +8 [G], +0x104 blocks left to read, +0x10C blocks in
// the stream, +0x148 the current segment record (0x20 each, from desc +0x10), +0x150 the ring buffer (+0x16C bytes),
// +0x154 its block entries (0x14 each: +0 the segment record, +4 block index, +8 the value used as a 0.32 fraction of
// a block by GetSamplePosUsingPos, +0xC its change (0 none), +0x10 state 1 free / 2 ready / 4 locked / 8 read
// requested, 0x10 last before a loop), +0x158 next to read, +0x160 oldest in use, +0x164 next to lock, +0x168 the
// lock tail, +0x170 count, +0x174 / +0x178 / +0x180 / +0x184 / +0x188 the second (reinterleaved) buffer's base /
// entries / first / next to lock / count, +0x198 blocks per read, +0x19C blocks kept free, +0x1A0 flags, +0x1A4 the
// semaphore.
extern "C" void RwaMul32By32(RwUInt32 a, RwUInt32 b, RwUInt32 *hi, RwUInt32 *lo);
extern "C" RwBool RwaSPU2RemoteIOPFopsPreviousOperationComplete(RwUInt32 handle, RwBool wait);
extern "C" RwBool _rwaStreamFileRequestQueueGet(void *q, RwUInt32 *out);
extern "C" RwBool _rwaStreamFileRequestQueueAdd(void *q, RwUInt32 w1, RwUInt32 w2, RwUInt32 w3, RwUInt32 w0,
                                                RwUInt32 w4);
extern "C" RwBool __rwaStreamSourceSeek(RwUInt8 *s, RwUInt32 p1, RwUInt32 p2);
extern "C" void _RwaStreamSourceReinterleave(RwUInt8 *s);
extern "C" void _RwaStreamSourceExecuteRequest(RwUInt8 *s);
extern "C" RwInt32 WaitSema(RwInt32 id);
extern "C" RwInt32 SignalSema(RwInt32 id);

static void func_002A87B0_inl1(RwUInt8 *s)
{
    if (_rwaGprogramContext == 2)
        WaitSema(func_002A7630_inl1(s, 0x1A4));
}

static void func_002A87B0_inl2(RwUInt8 *s)
{
    if (_rwaGprogramContext == 2)
        SignalSema(func_002A7630_inl1(s, 0x1A4));
}

// Entry i's word at off of the block entry array whose address is at s + arr.
static RwUInt32 &func_002A87B0_inl3(RwUInt8 *s, RwUInt32 arr, RwUInt32 i, RwUInt32 off)
{
    return func_002A7630_inl1((void *)(func_002A7630_inl1(s, arr) + i * 20), off);
}

// A block entry (+4 block index, +8 a 0.32 fraction of a block) as a sample position in format k.
// 0x002A8180
extern "C" RwUInt32 __rwaStreamSourceGetSamplePosUsingPos(RwUInt8 *s, RwUInt32 *pos, RwUInt32 k)
{
    RwUInt8 *fmt = (RwUInt8 *)(func_002A7630_inl1((void *)func_002A7630_inl1(s, 0x124), 0x18) + k * 40);
    RwUInt32 per = func_002A7630_inl1(fmt, 0xC) * (func_002A7630_inl1(fmt, 0x20) / *(RwUInt16 *)(fmt + 0x1A));
    RwUInt32 base = pos[1] * per;
    RwUInt32 hi, lo;
    RwaMul32By32(pos[2], per, &hi, &lo);
    RwUInt8 *wf = (RwUInt8 *)func_002A7630_inl1(fmt, 0x14);
    RwUInt32 ch = (wf[0x18] & 4) ? 1 : wf[0xD];
    return (base + hi) / ch;
}

// A packet's block is free again; in the ring the oldest-in-use index moves past the freed run.
// 0x002A8BA0
extern "C" void __rwaStreamSourceUnlockStreamBlock(RwUInt8 *s, RwUInt32 block)
{
    func_002A87B0_inl1(s);
    RwUInt32 bsz = func_002A7630_inl1((void *)func_002A7630_inl1(s, 0x124), 0x20);
    RwUInt32 base = func_002A7630_inl1(s, 0x150);
    if (block < base || !(block < base + func_002A7630_inl1(s, 0x16C))) {
        func_002A87B0_inl3(s, 0x178, (block - func_002A7630_inl1(s, 0x174)) / bsz, 0x10) = 1;
    } else {
        RwUInt32 idx = (block - base) / bsz;
        func_002A87B0_inl3(s, 0x154, idx, 0x10) = 1;
        RwUInt32 cnt = func_002A7630_inl1(s, 0x170);
        RwUInt32 n = idx + 1;
        if (n == cnt)
            n = 0;
        if (n == func_002A7630_inl1(s, 0x164)) {
            func_002A7630_inl1(s, 0x160) = func_002A7630_inl1(s, 0x168);
            func_002A7630_inl1(s, 0x164) = func_002A7630_inl1(s, 0x160);
        } else {
            RwUInt32 i = func_002A7630_inl1(s, 0x160);
            RwBool first = 1;
            while (func_002A87B0_inl3(s, 0x154, i, 0x10) & 1) {
                i++;
                if (i == cnt) {
                    i = 0;
                    if (!first)
                        break;
                    first = 0;
                }
            }
            func_002A7630_inl1(s, 0x160) = i;
        }
    }
    func_002A87B0_inl2(s);
}

// The next ready block (state 2; with flag 0x4000 any block marked 2, else also not 4), locked (4): its address,
// its entry into *pos; null when none.
// 0x002A8D10
extern "C" RwUInt32 __rwaStreamSourceLockStreamBlock(RwUInt8 *s, RwUInt32 *pos)
{
    func_002A87B0_inl1(s);
    RwUInt32 bsz = func_002A7630_inl1((void *)func_002A7630_inl1(s, 0x124), 0x20);
    RwUInt32 r;
    if (func_002A7630_inl1(s, 0x1A0) & 0x20000) {
        RwUInt32 f = func_002A87B0_inl3(s, 0x178, func_002A7630_inl1(s, 0x184), 0x10);
        if (func_002A7630_inl1(s, 0x1A0) & 0x4000) {
            if (!(f & 2)) {
                func_002A87B0_inl2(s);
                return 0;
            }
        } else if (!(f & 2) || (f & 4)) {
            func_002A87B0_inl2(s);
            return 0;
        }
        RwUInt32 i = func_002A7630_inl1(s, 0x184);
        RwUInt32 a = func_002A7630_inl1(s, 0x174);
        *pos = func_002A7630_inl1(s, 0x178) + i * 20;
        func_002A87B0_inl3(s, 0x178, func_002A7630_inl1(s, 0x184), 0x10) = 4;
        r = a + i * bsz;
        RwUInt32 n = func_002A7630_inl1(s, 0x184) + 1;
        if (n == func_002A7630_inl1(s, 0x188))
            n = 0;
        func_002A7630_inl1(s, 0x184) = n;
    } else {
        RwUInt32 f = func_002A87B0_inl3(s, 0x154, func_002A7630_inl1(s, 0x164), 0x10);
        if (func_002A7630_inl1(s, 0x1A0) & 0x4000) {
            if (!(f & 2)) {
                func_002A87B0_inl2(s);
                return 0;
            }
        } else if (!(f & 2) || (f & 4)) {
            func_002A87B0_inl2(s);
            return 0;
        }
        RwUInt32 i = func_002A7630_inl1(s, 0x164);
        RwUInt32 a = func_002A7630_inl1(s, 0x150);
        *pos = func_002A7630_inl1(s, 0x154) + i * 20;
        func_002A87B0_inl3(s, 0x154, func_002A7630_inl1(s, 0x164), 0x10) = 4;
        r = a + i * bsz;
        RwUInt32 old = func_002A7630_inl1(s, 0x164);
        RwUInt32 n = old + 1;
        if (n == func_002A7630_inl1(s, 0x170))
            n = 0;
        func_002A7630_inl1(s, 0x164) = n;
        if (func_002A7630_inl1(s, 0x168) == old)
            func_002A7630_inl1(s, 0x168) = n;
    }
    func_002A7630_inl1(s, 0x108)--;
    func_002A87B0_inl2(s);
    return r;
}

// Queues reads into the free part of the ring: up to +0x198 blocks (and the blocks left), as one or two file
// requests (split at the ring's end; each {0, buffer, bytes, 0, 4}); the blocks are marked requested (8) with their
// generation / index / segment. 1 queued, 2 the request queue is full, 3 no free slot (or fewer than all but +0x19C
// free), 4 end of stream; at the end with looping (0x8000) the stream seeks to 0 and reads again (0 if the seek fails).
// 0x002A87B0
extern "C" RwUInt32 __rwaStreamSourceRead(RwUInt8 *s)
{
    func_002A87B0_inl1(s);
    RwUInt32 cur = func_002A7630_inl1(s, 0x158);
    if (func_002A87B0_inl3(s, 0x154, cur, 0x10) != 1) {
        func_002A87B0_inl2(s);
        return 3;
    }
    RwUInt32 bsz = func_002A7630_inl1((void *)func_002A7630_inl1(s, 0x124), 0x20);
    RwUInt32 nfree;
    if (func_002A7630_inl1(s, 0x160) == cur)
        nfree = func_002A7630_inl1(s, 0x170);
    else if (func_002A7630_inl1(s, 0x160) < cur)
        nfree = func_002A7630_inl1(s, 0x170) - (cur - func_002A7630_inl1(s, 0x160));
    else
        nfree = func_002A7630_inl1(s, 0x160) - cur;
    if (nfree < func_002A7630_inl1(s, 0x170) - func_002A7630_inl1(s, 0x19C)) {
        func_002A87B0_inl2(s);
        return 3;
    }
    RwUInt32 left = func_002A7630_inl1(s, 0x104);
    if (!left) {
        func_002A87B0_inl2(s);
        if (func_002A7630_inl1(s, 0x1A0) & 0x8000) {
            if (__rwaStreamSourceSeek(s, 0, 0))
                return __rwaStreamSourceRead(s);
            return 0;
        }
        return 4;
    }
    RwUInt32 res = 1;
    RwUInt32 n = func_002A7630_inl1(s, 0x198);
    if (nfree < n)
        n = nfree;
    if (left < n)
        n = left;
    if (!(n < 0x80000000u))
        n = 0x7FFFFFFF;
    RwUInt32 off = cur * bsz;
    RwUInt32 bytes = bsz * n;
    RwUInt32 first = func_002A7630_inl1(s, 0x16C) - off;
    if (bytes < first)
        first = bytes;
    RwUInt32 second = bytes - first;
    RwInt32 got = 0;
    if (first) {
        if (!_rwaStreamFileRequestQueueAdd(s + 0x98, func_002A7630_inl1(s, 0x150) + off, first, 0, 0, 4)) {
            func_002A87B0_inl2(s);
            return 2;
        }
        got += first;
    }
    if (second) {
        if (_rwaStreamFileRequestQueueAdd(s + 0x98, func_002A7630_inl1(s, 0x150), second, 0, 0, 4))
            got += second;
        else
            res = 2;
    }
    while (got) {
        RwUInt32 i = func_002A7630_inl1(s, 0x158);
        RwUInt32 *e = (RwUInt32 *)(func_002A7630_inl1(s, 0x154) + i * 20);
        e[1] = func_002A7630_inl1(s, 0x10C) - func_002A7630_inl1(s, 0x104);
        e[0] = func_002A7630_inl1(s, 0x148);
        e[4] = 8;
        if (func_002A7630_inl1(s, 0xFC) == func_002A7630_inl1(s, 0x100)) {
            e[3] = 0;
        } else {
            e[3] = func_002A7630_inl1(s, 0x100);
            func_002A7630_inl1(s, 0xFC) = func_002A7630_inl1(s, 0x100);
        }
        e[2] = func_002A7630_inl1(s, 0xFC);
        func_002A7630_inl1(s, 0x104)--;
        RwUInt32 m = func_002A7630_inl1(s, 0x158) + 1;
        got -= bsz;
        if (m == func_002A7630_inl1(s, 0x170))
            m = 0;
        func_002A7630_inl1(s, 0x158) = m;
    }
    if (!func_002A7630_inl1(s, 0x104) && func_002A7630_inl1(s, 0xF8) != 0xFFFFFFFF &&
        (func_002A7630_inl1(s, 0x1A0) & 0x8000)) {
        RwUInt32 p = func_002A7630_inl1(s, 0x158) ? func_002A7630_inl1(s, 0x158) - 1 : func_002A7630_inl1(s, 0x170) - 1;
        func_002A87B0_inl3(s, 0x154, p, 0x10) |= 0x10;
    }
    func_002A87B0_inl2(s);
    return res;
}

// Per update: the next queued file request goes to the IOP once the previous file operation is complete (and no
// operation flag 0x5FF0 / 0x40000 is up); then the switch into / out of the reinterleave buffer (0x20000) at a
// segment change or a loop end.
// 0x002A9890
extern "C" void __rwaStreamSourceProcess(RwUInt8 *s)
{
    RwBool empty = *(RwUInt16 *)(s + 0x9E) == *(RwUInt16 *)(s + 0xA0);
    if (empty)
        empty = !*(RwUInt16 *)(s + 0xA2);
    if (!empty && !(func_002A7630_inl1(s, 0x1A0) & 0x5FF0) && !(func_002A7630_inl1(s, 0x1A0) & 0x40000) &&
        RwaSPU2RemoteIOPFopsPreviousOperationComplete(func_002A7630_inl1(s, 0x84), 0) == 1 &&
        _rwaStreamFileRequestQueueGet(s + 0x98, (RwUInt32 *)(s + 0xE4)))
        _RwaStreamSourceExecuteRequest(s);
    if (func_002A87B0_inl3(s, 0x154, func_002A7630_inl1(s, 0x168), 0x10) & 1)
        return;
    if (!func_002A7630_inl1(s, 0x174))
        return;
    if (func_002A87B0_inl3(s, 0x178, func_002A7630_inl1(s, 0x180), 0x10) & 6)
        return;
    if (func_002A87B0_inl3(s, 0x154, func_002A7630_inl1(s, 0x160), 0xC) ||
        func_002A87B0_inl3(s, 0x178, func_002A7630_inl1(s, 0x180), 0xC) ||
        ((func_002A87B0_inl3(s, 0x154, func_002A7630_inl1(s, 0x160), 0x10) & 0x10) &&
         func_002A7630_inl1(s, 0xF8) != 0xFFFFFFFF)) {
        func_002A7630_inl1(s, 0x1A0) |= 0x20000;
        _RwaStreamSourceReinterleave(s);
        return;
    }
    if (func_002A87B0_inl3(s, 0x178, func_002A7630_inl1(s, 0x184), 0x10) == 1)
        func_002A7630_inl1(s, 0x1A0) &= 0xFFFDFFFF;
}

typedef void (*UnkFn_002A9750)(RwUInt32 handle, RwUInt32 result, RwUInt32 status, RwUInt32 kind, RwUInt32 arg);
extern "C" void _RwaStreamSourceFileOpCallback(RwUInt32 handle, RwUInt32 result, RwUInt32 status, RwUInt32 kind,
                                               RwUInt8 *s);
extern "C" void RwaSPU2RemoteIOPFopsSeek(RwUInt32 handle, RwUInt32 pos, UnkFn_002A9750 cb, RwUInt32 arg);
extern "C" void RwaSPU2RemoteIOPFopsRead(RwUInt32 handle, RwUInt32 buffer, RwUInt32 offset, RwUInt32 size,
                                         RwUInt32 *done, UnkFn_002A9750 cb, RwUInt32 arg);
extern "C" void func_002A1980(void *reader, RwUInt32 pos, UnkFn_002A9750 cb, RwUInt32 arg);
extern "C" void func_002A19C0(void *reader, RwUInt32 buf, RwUInt32 size, UnkFn_002A9750 cb, RwUInt32 arg);
extern "C" void func_002A19E0(void *reader);
extern "C" void _rwaStreamFileRequestQueueEmpty(void *q);

// The request at +0xE4 ({0, buffer, bytes, file position, 1 seek | 4 read}): its read first (flag 0x10 up until
// the read completes), else its seek (0x40); straight to the IOP for an IOP stream (+0x18C == 2: a read of
// sound-RAM data at offset 0x1000 of the upload buffer [G]), else through the RwStream reader at +0x28.
// 0x002A9750
extern "C" void _RwaStreamSourceExecuteRequest(RwUInt8 *s)
{
    if (func_002A7630_inl1(s, 0x1A0) & 0x5FF0)
        return;
    if (!RwaSPU2RemoteIOPFopsPreviousOperationComplete(func_002A7630_inl1(s, 0x84), 0))
        return;
    RwUInt32 *req = (RwUInt32 *)(s + 0xE4);
    RwUInt32 f = req[4];
    if (f & 4) {
        req[4] = f & ~4u;
        func_002A7630_inl1(s, 0x1A0) |= 0x10;
        if (func_002A7630_inl1(s, 0x18C) == 2)
            RwaSPU2RemoteIOPFopsRead(func_002A7630_inl1(s, 0x84), req[1], 0x1000, req[2], 0,
                                     (UnkFn_002A9750)_RwaStreamSourceFileOpCallback, (unsigned int)s);
        else
            func_002A19C0(s + 0x28, req[1], req[2], (UnkFn_002A9750)_RwaStreamSourceFileOpCallback, (unsigned int)s);
    } else if (f & 1) {
        req[4] = f & ~1u;
        func_002A7630_inl1(s, 0x1A0) |= 0x40;
        if (func_002A7630_inl1(s, 0x18C) == 2)
            RwaSPU2RemoteIOPFopsSeek(func_002A7630_inl1(s, 0x84), req[3],
                                     (UnkFn_002A9750)_RwaStreamSourceFileOpCallback, (unsigned int)s);
        else
            func_002A1980(s + 0x28, req[3], (UnkFn_002A9750)_RwaStreamSourceFileOpCallback, (unsigned int)s);
    }
}

// Drops every queued and in-flight read (flag 0x10000 = the source is open): the request queue emptied, the reader's
// read cancelled, requested blocks freed, every ready block locked and unlocked, the ring reset.
// 0x002A8220
extern "C" void __rwaStreamSourceFlush(RwUInt8 *s)
{
    if (!(func_002A7630_inl1(s, 0x1A0) & 0x10000))
        return;
    _rwaStreamFileRequestQueueEmpty(s + 0x98);
    func_002A19E0(s + 0x28);
    func_002A7630_inl1(s, 0x1A0) &= ~0x50u;
    func_002A87B0_inl1(s);
    func_002A7630_inl1(s, 0x1A0) |= 0x4000;
    while (func_002A87B0_inl3(s, 0x154, func_002A7630_inl1(s, 0x15C), 0x10) & 8) {
        RwUInt32 i = func_002A7630_inl1(s, 0x15C);
        func_002A7630_inl1(s, 0x15C) = i + 1;
        func_002A87B0_inl3(s, 0x154, i, 0x10) = 1;
        if (func_002A7630_inl1(s, 0x15C) == func_002A7630_inl1(s, 0x170))
            func_002A7630_inl1(s, 0x15C) = 0;
    }
    func_002A87B0_inl2(s);
    RwUInt32 pos, b;
    do {
        b = __rwaStreamSourceLockStreamBlock(s, &pos);
        if (b)
            __rwaStreamSourceUnlockStreamBlock(s, b);
    } while (b);
    if (func_002A7630_inl1(s, 0x1A0) & 0x20000)
        for (RwUInt32 i = 0; i < func_002A7630_inl1(s, 0x170); i++)
            func_002A87B0_inl3(s, 0x154, i, 0x10) = 1;
    if (func_002A7630_inl1(s, 0x178))
        func_002A87B0_inl3(s, 0x178, func_002A7630_inl1(s, 0x180), 0xC) = 0;
    func_002A7630_inl1(s, 0x160) = 0;
    func_002A7630_inl1(s, 0x164) = 0;
    func_002A7630_inl1(s, 0x15C) = 0;
    func_002A7630_inl1(s, 0x158) = 0;
    func_002A7630_inl1(s, 0x168) = 0;
    func_002A7630_inl1(s, 0x1A0) &= ~0x4000u;
}

typedef RwUInt32 (*UnkFn_002AA4BC)(RwUInt32 size, RwUInt32 arg);
typedef void (*UnkFn_002AA66C)(RwUInt32 p, RwUInt32 arg);
typedef void (*UnkFn_002AA0C8)(RwUInt8 *s, RwUInt32 result, RwUInt32 arg);
typedef RwInt32 (*UnkFn_002AA5B8)(RwUInt32 a, RwUInt32 b);
typedef RwInt32 (*UnkFn_002AAB50)(RwUInt32 file);
extern RwUInt8 ourGlobals[0x200];             // RenderWare's globals: +0x114 the string compare [L]
extern "C" RwUInt8 *func_001D8B88(void);      // RwOsGetFileInterface [E map]
extern "C" RwUInt32 sceSifAllocSysMemory(RwInt32 mode, RwInt32 size, RwUInt32 addr);
extern "C" void *func_00293470(RwUInt32 size, RwUInt32 hint);
extern "C" RwInt32 RwaUUIDCompare(const RwaUUID *a, const RwaUUID *b);
extern "C" RwaUniqueID *_rwaUniqueIDInitialize(RwaUniqueID *id);
extern "C" RwUInt32 _rwaRwStreamGetPos(void *reader);
extern "C" void _rwaRwStreamFindChunk(RwUInt32 *stream, void *reader, RwUInt32 type, RwUInt32 *header,
                                      UnkFn_002A9750 cb, RwUInt32 arg);
extern "C" void _rwaRwStreamReadChunkHeader(void *reader, RwUInt32 *header, UnkFn_002A9750 cb, RwUInt32 arg);
extern "C" RwUInt8 *_rwaStreamFormatCreateFromPack(RwUInt8 *desc);
extern "C" void _RwaStreamSourceFree(RwUInt8 *s);
extern "C" RwUInt32 _RwaStreamSourceGetLastBlockSize(RwUInt8 *s);

// The open failed: the owner's callback (+0x11C, its argument +0x120) gets status | code, then the source is freed.
static void func_002A9CF0_inl1(RwUInt8 *s, RwUInt32 code)
{
    if (func_002A7630_inl1(s, 0x11C))
        (*(UnkFn_002AA0C8 *)(s + 0x11C))(s, code, func_002A7630_inl1(s, 0x120));
    _RwaStreamSourceFree(s);
}

// The stream description buffer (+0x124) back to the owner's free function (+0x114, argument +0x118).
static void func_002A9CF0_inl2(RwUInt8 *s)
{
    (*(UnkFn_002AA66C *)(s + 0x114))(func_002A7630_inl1(s, 0x124), func_002A7630_inl1(s, 0x118));
    func_002A7630_inl1(s, 0x124) = 0;
}

// Every file operation of the source completes here (status 4 = failed). Flag 0x10: a ring read done -- the blocks
// it covered become ready (2; a loop's last block 0x12, with |4 when a reinterleave buffer exists and a loop segment is
// set). 0x40: a seek done. The open sequence: 0x20 opened -> find chunk +0x8C (flag 8, 0x80); 0x80 chunk 0x80D ->
// find 0x80E, chunk 0x80E -> read its 0x14-byte header to +0x128 (0x100); 0x100 -> read the id / name (0x1000), or,
// once matched, the description (+0x128 bytes into a buffer from +0x110: 0x200); 0x1000 -> compare with the wanted
// UUID (+0x13C) / name (+0x140): a match seeks back to the description, a mismatch skips to the next 0x80D chunk;
// 0x200 -> _rwaStreamFormatCreateFromPack, seek past the chunk (0x400); 0x400 -> read the next chunk header (0x800);
// 0x800 chunk 0x80F (the data) -> the buffers (+0x18C == 2: in IOP memory) and ring set up, seek to 0, process, and
// the owner's callback with 0 (or 1 on status 4). After a read / seek the next queued request starts; with flag
// 0x40000 the file position is fetched instead.
// 0x002A9CF0
extern "C" void _RwaStreamSourceFileOpCallback(RwUInt32 handle, RwUInt32 result, RwUInt32 status, RwUInt32 kind,
                                               RwUInt8 *s)
{
    RwUInt32 &fl = func_002A7630_inl1(s, 0x1A0);
    if (fl & 0x10) {
        RwUInt32 bsz = func_002A7630_inl1((void *)func_002A7630_inl1(s, 0x124), 0x20);
        fl &= ~0x10u;
        func_002A87B0_inl1(s);
        if (fl & 0x4000) {
            func_002A87B0_inl2(s);
            return;
        }
        while (result) {
            RwUInt32 i = func_002A7630_inl1(s, 0x15C);
            if (func_002A87B0_inl3(s, 0x154, i, 0x10) & 0x10) {
                func_002A87B0_inl3(s, 0x154, func_002A7630_inl1(s, 0x15C), 0x10) = 0x12;
                if (func_002A7630_inl1(s, 0x174) && func_002A7630_inl1(s, 0xF8) != 0xFFFFFFFF)
                    func_002A87B0_inl3(s, 0x154, func_002A7630_inl1(s, 0x15C), 0x10) |= 4;
            } else {
                func_002A87B0_inl3(s, 0x154, func_002A7630_inl1(s, 0x15C), 0x10) = 2;
            }
            RwUInt32 n = func_002A7630_inl1(s, 0x15C) + 1;
            if (n == func_002A7630_inl1(s, 0x170))
                n = 0;
            func_002A7630_inl1(s, 0x15C) = n;
            result -= bsz;
        }
        func_002A87B0_inl2(s);
    } else if (fl & 0x40) {
        fl &= ~0x40u;
    } else if (fl & 0x20) {
        fl &= ~0x20u;
        if (status == 4) {
            func_002A9CF0_inl1(s, status | 2);
            return;
        }
        fl |= 8;
        fl |= 0x80;
        _rwaRwStreamFindChunk((RwUInt32 *)func_002A7630_inl1(s, 0x28), s + 0x28, func_002A7630_inl1(s, 0x8C), (RwUInt32 *)(s + 0x8C),
                              (UnkFn_002A9750)_RwaStreamSourceFileOpCallback, (unsigned int)s);
        return;
    } else if (fl & 0x80) {
        fl &= ~0x80u;
        if (status != 4) {
            RwUInt32 t = func_002A7630_inl1(s, 0x8C);
            if (t == 0x80D) {
                fl |= 0x80;
                _rwaRwStreamFindChunk((RwUInt32 *)func_002A7630_inl1(s, 0x28), s + 0x28, 0x80E, (RwUInt32 *)(s + 0x8C),
                                      (UnkFn_002A9750)_RwaStreamSourceFileOpCallback, (unsigned int)s);
                return;
            }
            if (t == 0x80E) {
                fl |= 0x100;
                func_002A19C0(s + 0x28, (unsigned int)(s + 0x128), 0x14, (UnkFn_002A9750)_RwaStreamSourceFileOpCallback,
                              (unsigned int)s);
                return;
            }
        }
        func_002A9CF0_inl1(s, status | 1);
        return;
    } else if (fl & 0x100) {
        RwUInt32 size = 0;
        fl &= ~0x100u;
        if (status == 4) {
            func_002A9CF0_inl1(s, status | 1);
            return;
        }
        if (!func_002A7630_inl1(s, 0x140) && !func_002A7630_inl1(s, 0x13C)) {
            size = func_002A7630_inl1(s, 0x128);
            fl |= 0x200;
        } else {
            if (func_002A7630_inl1(s, 0x130))
                size += 0x10;
            if (func_002A7630_inl1(s, 0x138))
                size += func_002A7630_inl1(s, 0x138);
            fl |= 0x1000;
        }
        func_002A7630_inl1(s, 0x124) = (*(UnkFn_002AA4BC *)(s + 0x110))(size, func_002A7630_inl1(s, 0x118));
        if (func_002A7630_inl1(s, 0x124)) {
            func_002A19C0(s + 0x28, func_002A7630_inl1(s, 0x124), size, (UnkFn_002A9750)_RwaStreamSourceFileOpCallback,
                          (unsigned int)s);
            return;
        }
        func_002A9CF0_inl1(s, status | 1);
        return;
    } else if (fl & 0x1000) {
        RwUInt32 uuid = 0, name = 0;
        RwBool match = 0;
        fl &= ~0x1000u;
        if (status == 4) {
            func_002A9CF0_inl2(s);
            func_002A9CF0_inl1(s, status | 1);
            return;
        }
        RwUInt32 us = func_002A7630_inl1(s, 0x130);
        if (us)
            uuid = func_002A7630_inl1(s, 0x124);
        if (func_002A7630_inl1(s, 0x138))
            name = func_002A7630_inl1(s, 0x124) + us;
        if (uuid && func_002A7630_inl1(s, 0x13C) &&
            !RwaUUIDCompare((const RwaUUID *)uuid, (const RwaUUID *)func_002A7630_inl1(s, 0x13C)))
            match = 1;
        if (name && func_002A7630_inl1(s, 0x140) &&
            !(*(UnkFn_002AA5B8 *)(ourGlobals + 0x114))(name, func_002A7630_inl1(s, 0x140)))
            match = 1;
        func_002A9CF0_inl2(s);
        if (!match) {
            fl |= 0x20;
            func_002A7630_inl1(s, 0x8C) = 0x80D;
            RwUInt32 skip = func_002A7630_inl1(s, 0x90) -
                            (func_002A7630_inl1(s, 0x138) + 0x14 + func_002A7630_inl1(s, 0x130));
            RwUInt32 pos = _rwaRwStreamGetPos(s + 0x28);
            func_002A1980(s + 0x28, skip + pos, (UnkFn_002A9750)_RwaStreamSourceFileOpCallback, (unsigned int)s);
        } else {
            fl |= 0x100;
            _rwaUniqueIDInitialize((RwaUniqueID *)(s + 0x13C));
            RwUInt32 pos = _rwaRwStreamGetPos(s + 0x28);
            func_002A1980(s + 0x28, pos - (func_002A7630_inl1(s, 0x130) + func_002A7630_inl1(s, 0x138)),
                          (UnkFn_002A9750)_RwaStreamSourceFileOpCallback, (unsigned int)s);
        }
        return;
    } else if (fl & 0x200) {
        fl &= ~0x200u;
        if (status == 4) {
            func_002A9CF0_inl1(s, status | 1);
            return;
        }
        if (!_rwaStreamFormatCreateFromPack((RwUInt8 *)func_002A7630_inl1(s, 0x124))) {
            func_002A9CF0_inl2(s);
            func_002A9CF0_inl1(s, status | 1);
            return;
        }
        fl |= 0x400;
        RwUInt32 skip = func_002A7630_inl1(s, 0x90) - (func_002A7630_inl1(s, 0x128) + 0x14);
        RwUInt32 pos = _rwaRwStreamGetPos(s + 0x28);
        func_002A1980(s + 0x28, skip + pos, (UnkFn_002A9750)_RwaStreamSourceFileOpCallback, (unsigned int)s);
        return;
    } else if (fl & 0x400) {
        fl &= ~0x400u;
        if (status == 4) {
            func_002A9CF0_inl2(s);
            func_002A9CF0_inl1(s, status | 1);
            return;
        }
        fl |= 0x800;
        _rwaRwStreamReadChunkHeader(s + 0x28, (RwUInt32 *)(s + 0x8C), (UnkFn_002A9750)_RwaStreamSourceFileOpCallback,
                                    (unsigned int)s);
        return;
    } else if (fl & 0x800) {
        RwUInt8 *desc = (RwUInt8 *)func_002A7630_inl1(s, 0x124);
        fl &= ~0x800u;
        RwUInt32 bsz = func_002A7630_inl1(desc, 0x20);
        if (status == 4 || func_002A7630_inl1(s, 0x8C) != 0x80F) {
            func_002A9CF0_inl2(s);
            func_002A9CF0_inl1(s, status | 1);
            return;
        }
        RwBool iop = 0;
        if (!func_002A7630_inl1(s, 0x150)) {
            if (!func_002A7630_inl1(s, 0x16C)) {
                RwUInt32 nb = func_002A7630_inl1(s, 0x198);
                RwUInt32 extra = 0;
                if (!nb)
                    nb = 2;
                if (!func_002A7630_inl1(s, 0x154) && func_002A7630_inl1(s, 0x18C) != 2)
                    extra += 0x38;
                func_002A7630_inl1(s, 0x16C) = (extra + bsz) * nb;
            }
            if (func_002A7630_inl1(s, 0x18C) == 2)
                func_002A7630_inl1(s, 0x150) = sceSifAllocSysMemory(0, func_002A7630_inl1(s, 0x16C), 0);
            else
                func_002A7630_inl1(s, 0x150) = (unsigned int)func_00293470(func_002A7630_inl1(s, 0x16C), 0x3080F);
            RwUInt32 ok = func_002A7630_inl1(s, 0x150);
            if (!ok)
                _RwaStreamSourceFree(s);
            if (!ok) {
                if (func_002A7630_inl1(s, 0x11C))
                    (*(UnkFn_002AA0C8 *)(s + 0x11C))(s, status | 1, func_002A7630_inl1(s, 0x120));
                return;
            }
        }
        if (func_002A7630_inl1(s, 0x17C) || func_002A7630_inl1(s, 0x188)) {
            if (!func_002A7630_inl1(s, 0x174)) {
                RwUInt32 n2 = func_002A7630_inl1(s, 0x188);
                if (!func_002A7630_inl1(s, 0x17C) && n2) {
                    RwUInt32 sz = bsz * n2;
                    if (func_002A7630_inl1(s, 0x18C) != 2)
                        sz += n2 * 20;
                    func_002A7630_inl1(s, 0x17C) = sz;
                }
                RwUInt32 total = func_002A7630_inl1(s, 0x17C);
                if (func_002A7630_inl1(s, 0x18C) == 2) {
                    RwUInt32 t = func_002A7630_inl1(desc, 0x14) * 72;
                    func_002A7630_inl1(s, 0x190) = (unsigned int)func_00293470(t, 0x3080F);
                    total += t;
                }
                if (func_002A7630_inl1(s, 0x18C) == 2)
                    func_002A7630_inl1(s, 0x174) = sceSifAllocSysMemory(0, total, 0);
                else
                    func_002A7630_inl1(s, 0x174) = (unsigned int)func_00293470(total, 0x3080F);
                RwUInt32 ok = func_002A7630_inl1(s, 0x174) ? func_002A7630_inl1(s, 0x150) : 0;
                if (!func_002A7630_inl1(s, 0x174))
                    _RwaStreamSourceFree(s);
                if (!ok) {
                    if (func_002A7630_inl1(s, 0x11C))
                        (*(UnkFn_002AA0C8 *)(s + 0x11C))(s, status | 1, func_002A7630_inl1(s, 0x120));
                    return;
                }
                if (func_002A7630_inl1(s, 0x18C) == 2)
                    func_002A7630_inl1(s, 0x194) = func_002A7630_inl1(s, 0x174) + func_002A7630_inl1(s, 0x17C);
            } else if (func_002A7630_inl1(s, 0x18C) == 2) {
                RwUInt32 t = func_002A7630_inl1(desc, 0x14) * 36;
                func_002A7630_inl1(s, 0x194) = sceSifAllocSysMemory(0, t, 0);
                func_002A7630_inl1(s, 0x190) = (unsigned int)func_00293470(t, 0x3080F);
                fl |= 4;
            }
        }
        if (func_002A7630_inl1(s, 0x18C) == 2) {
            func_002A7630_inl1(s, 0x170) = func_002A7630_inl1(s, 0x16C) / bsz;
            func_002A7630_inl1(s, 0x188) = func_002A7630_inl1(s, 0x17C) / bsz;
        } else {
            func_002A7630_inl1(s, 0x170) = func_002A7630_inl1(s, 0x16C) / (bsz + 0x38);
            func_002A7630_inl1(s, 0x188) = func_002A7630_inl1(s, 0x17C) / (bsz + 0x14);
        }
        func_002A7630_inl1(s, 0x16C) = func_002A7630_inl1(s, 0x170) * bsz;
        if (!func_002A7630_inl1(s, 0x154)) {
            if (func_002A7630_inl1(s, 0x18C) == 2)
                func_002A7630_inl1(s, 0x154) =
                    (unsigned int)func_002934C0(0x38 * func_002A7630_inl1(s, 0x170), 0x3080F);
            else
                func_002A7630_inl1(s, 0x154) = func_002A7630_inl1(s, 0x150) + func_002A7630_inl1(s, 0x16C);
        }
        func_002A7630_inl1(s, 0x1AC) = func_002A7630_inl1(s, 0x154) + func_002A7630_inl1(s, 0x170) * 20;
        func_002A7630_inl1(s, 0x1B0) = func_002A7630_inl1(s, 0x1AC) + func_002A7630_inl1(s, 0x170) * 16;
        func_002A7630_inl1(s, 0x17C) = func_002A7630_inl1(s, 0x188) * bsz;
        if (!func_002A7630_inl1(s, 0x17C)) {
            if (!(fl & 2) && func_002A7630_inl1(s, 0x174))
                func_00293440((void *)func_002A7630_inl1(s, 0x174));
            func_002A7630_inl1(s, 0x174) = 0;
        }
        if (func_002A7630_inl1(s, 0x18C) != 2)
            func_002A7630_inl1(s, 0x178) = func_002A7630_inl1(s, 0x174) + func_002A7630_inl1(s, 0x17C);
        else if (func_002A7630_inl1(s, 0x188))
            func_002A7630_inl1(s, 0x178) = (unsigned int)func_002934C0(func_002A7630_inl1(s, 0x188) * 20, 0x3080F);
        else
            func_002A7630_inl1(s, 0x178) = 0;
        if (!func_002A7630_inl1(s, 0x19C)) {
            func_002A7630_inl1(s, 0x19C) = func_002A7630_inl1(s, 0x170) >> 1;
            if (!func_002A7630_inl1(s, 0x19C))
                func_002A7630_inl1(s, 0x19C) = 1;
        }
        if (!(func_002A7630_inl1(s, 0x19C) < func_002A7630_inl1(s, 0x170)))
            func_002A7630_inl1(s, 0x19C) = func_002A7630_inl1(s, 0x170);
        if (!func_002A7630_inl1(s, 0x198)) {
            func_002A7630_inl1(s, 0x198) = func_002A7630_inl1(s, 0x170) >> 1;
            if (!func_002A7630_inl1(s, 0x198))
                func_002A7630_inl1(s, 0x198) = 1;
        }
        if (!(func_002A7630_inl1(s, 0x198) < func_002A7630_inl1(s, 0x170)))
            func_002A7630_inl1(s, 0x198) = func_002A7630_inl1(s, 0x170);
        RwUInt32 n = func_002A7630_inl1(s, 0x188);
        for (RwUInt32 i = 0; i < n; i++) {
            func_002A87B0_inl3(s, 0x178, i, 0xC) = 0;
            func_002A87B0_inl3(s, 0x178, i, 0x10) = 1;
        }
        n = func_002A7630_inl1(s, 0x170);
        for (RwUInt32 i = 0; i < n; i++) {
            func_002A87B0_inl3(s, 0x1B0, i, 0) = 0;
            func_002A87B0_inl3(s, 0x154, i, 0xC) = 0;
            func_002A87B0_inl3(s, 0x154, i, 0x10) = 1;
        }
        func_002A7630_inl1(s, 0x148) = func_002A7630_inl1((void *)func_002A7630_inl1(s, 0x124), 0x10);
        func_002A7630_inl1(s, 0x14C) = func_002A7630_inl1(s, 0x148);
        func_002A7630_inl1(s, 0xF8) = _RwaStreamSourceGetLastBlockSize(s);
        fl |= 0x10000;
        __rwaStreamSourceSeek(s, 0, 0);
        __rwaStreamSourceProcess(s);
        if (func_002A7630_inl1(s, 0x11C))
            (*(UnkFn_002AA0C8 *)(s + 0x11C))(s, status == 4, func_002A7630_inl1(s, 0x120));
        return;
    }
    if (fl & 0x40000) {
        RwUInt8 *fi = func_001D8B88();
        func_002A7630_inl1(s, 0x1A8) =
            (*(UnkFn_002AAB50 *)(fi + 0x28))(func_002A7630_inl1((void *)func_002A7630_inl1(s, 0x28), 0xC));
        return;
    }
    if (*(RwUInt16 *)(s + 0x9E) == *(RwUInt16 *)(s + 0xA0) && !*(RwUInt16 *)(s + 0xA2) &&
        !func_002A7630_inl1(s, 0xF4))
        return;
    if (func_002A7630_inl1(s, 0xF4) || _rwaStreamFileRequestQueueGet(s + 0x98, (RwUInt32 *)(s + 0xE4)))
        _RwaStreamSourceExecuteRequest(s);
}

extern "C" void __rwaStreamSourceSetSegmentByName(RwUInt8 *s, RwUInt32 name);
extern "C" void __rwaStreamSourceSetSegmentByUUID(RwUInt8 *s, RwUInt32 uuid);

// Seeks to byte `pos` of format k's data in the segment chosen (+0x14C): queues a seek request {0, 0, 0, file
// offset, 1} (file offset = segment +0x1C + description +0x24 + block * block size), takes over the segment (+0x148;
// its last block size into +0xF8), the start fraction within the block (+0x100) and the block counts (+0x10C total,
// +0x104 to read, +0x108 to lock). 0 when the request queue is full. Retail also rounds pos up to every format's frame
// size in a loop whose result is unused [C 0x2A84F8-0x2A8554] (left out: no effect).
// 0x002A83E0
extern "C" RwBool __rwaStreamSourceSeek(RwUInt8 *s, RwUInt32 k, RwUInt32 pos)
{
    RwUInt8 *fmt = (RwUInt8 *)(func_002A7630_inl1((void *)func_002A7630_inl1(s, 0x124), 0x18) + k * 40);
    RwUInt32 blk = pos / func_002A7630_inl1(fmt, 0x20);
    RwUInt32 hi, lo;
    RwaMul32By32(blk, func_002A7630_inl1(fmt, 0x20), &hi, &lo);
    RwUInt32 off = pos - lo;
    RwUInt8 *wf = (RwUInt8 *)func_002A7630_inl1(fmt, 0x14);
    RwUInt32 ch = (wf[0x18] & 4) ? 1 : wf[0xD];
    RwUInt32 unit = *(RwUInt16 *)(fmt + 0x1A) * ch;
    if (off % unit)
        off = unit * ((off + unit - 1) / unit);
    off = off / (*(RwUInt16 *)(fmt + 0x1A) * ch);
    RwUInt32 frac;
    RwaMul32By32(off, 0xFFFFFFFF / (func_002A7630_inl1(fmt, 0x20) / (*(RwUInt16 *)(fmt + 0x1A) * ch)), &hi, &frac);
    if (frac & ~1u)
        frac = frac + 1;
    RwUInt8 *desc = (RwUInt8 *)func_002A7630_inl1(s, 0x124);
    RwUInt32 fpos = func_002A7630_inl1((void *)func_002A7630_inl1(s, 0x14C), 0x1C) +
                    (func_002A7630_inl1(desc, 0x24) + blk * func_002A7630_inl1(desc, 0x20));
    func_002A87B0_inl1(s);
    if (!_rwaStreamFileRequestQueueAdd(s + 0x98, 0, 0, fpos, 0, 1)) {
        func_002A87B0_inl2(s);
        return 0;
    }
    if (func_002A7630_inl1(s, 0x148) != func_002A7630_inl1(s, 0x14C)) {
        func_002A7630_inl1(s, 0x148) = func_002A7630_inl1(s, 0x14C);
        func_002A7630_inl1(s, 0xF8) = _RwaStreamSourceGetLastBlockSize(s);
    }
    func_002A7630_inl1(s, 0x100) = frac;
    func_002A7630_inl1(s, 0x10C) = func_002A7630_inl1((void *)func_002A7630_inl1(s, 0x148), 0x18) /
                                   func_002A7630_inl1((void *)func_002A7630_inl1(s, 0x124), 0x20);
    func_002A7630_inl1(s, 0x104) = func_002A7630_inl1(s, 0x10C) - blk;
    func_002A7630_inl1(s, 0x108) = func_002A7630_inl1(s, 0x10C) - blk;
    func_002A87B0_inl2(s);
    return 1;
}

// The loop end within the last block of the current segment (+0x148: per-format byte sizes at +0xC): the largest
// format's remainder as a 0.32 fraction of a block, + 1; -1 when it ends on a block boundary.
// 0x002A86A0
extern "C" RwUInt32 _RwaStreamSourceGetLastBlockSize(RwUInt8 *s)
{
    RwUInt32 best = 0, largest = 0;
    RwUInt8 *seg = (RwUInt8 *)func_002A7630_inl1(s, 0x148);
    RwUInt8 *fmts = (RwUInt8 *)func_002A7630_inl1((void *)func_002A7630_inl1(s, 0x124), 0x18);
    for (RwUInt32 i = 0; i < func_002A7630_inl1((void *)func_002A7630_inl1(s, 0x124), 0x14); i++) {
        RwUInt32 sz = ((RwUInt32 *)func_002A7630_inl1(seg, 0xC))[i];
        if (largest < sz) {
            largest = sz;
            RwUInt8 *f = fmts + i * 40;
            RwUInt32 rem = sz % func_002A7630_inl1(f, 0x20);
            RwUInt32 hi, lo;
            RwaMul32By32(rem, 0xFFFFFFFF / func_002A7630_inl1(f, 0x20), &hi, &lo);
            lo = lo + 1;
            if (best < lo)
                best = lo;
        }
    }
    if (best == 1)
        best = 0xFFFFFFFF;
    return best;
}

// A new stream position {position, flush}: position = {segment (index / UUID / name), kind 3 / 2 / 1, value,
// units 1 seconds (rounded up to the format's frame) / else samples}; flushes first when asked, then seeks.
// 0x002A74D0
extern "C" RwBool _rwaStreamSourceSetStreamPos(RwaObj *obj, RwUInt32 *pos)
{
    RwUInt8 *s = (RwUInt8 *)obj + 0x74;
    if (pos[1])
        __rwaStreamSourceFlush(s);
    RwUInt32 *q = (RwUInt32 *)pos[0];
    RwUInt8 *desc = (RwUInt8 *)func_002A7630_inl1(s, 0x124);
    RwUInt8 *fmt = (RwUInt8 *)func_002A7630_inl1(desc, 0x18);
    RwUInt32 samples;
    if (q[3] == 1) {
        RwUInt32 rate = *(RwUInt32 *)func_002A7630_inl1(fmt, 0x14);
        RwReal t = *(RwReal *)&q[2] * (RwReal)rate;
        RwUInt32 a = func_002A7630_inl1(fmt, 0xC) - 1;
        samples = ~a & ((int)t + a);
    } else {
        samples = q[2];
    }
    RwUInt32 frame = *(RwUInt16 *)(fmt + 0x1A) * ((RwUInt8 *)func_002A7630_inl1(fmt, 0x14))[0xD];
    RwUInt32 bytes = (samples / func_002A7630_inl1(fmt, 0xC)) * frame;
    RwUInt32 kind = q[1];
    if (kind == 3)
        func_002A7630_inl1(s, 0x14C) = func_002A7630_inl1(desc, 0x10) + q[0] * 32;
    else if (kind == 2)
        __rwaStreamSourceSetSegmentByUUID(s, q[0]);
    else if (kind == 1)
        __rwaStreamSourceSetSegmentByName(s, q[0]);
    return __rwaStreamSourceSeek(s, 0, bytes);
}

// ---- creation / teardown ----
extern RwaObjDef D_01E7DF10;               // the stream source object definition (static struct)
extern RwaObjDefInterfaceInfo D_01E7DEF0;  // its interface info [G type]
extern RwLinkList _streamsourceAliasInfo;
extern RwaObjDef *_rwaGnullObjDef;
extern RwaInputParamDef _streamsourceInputParams[5];
extern RwaOutputParamDef _streamsourceOutputParams[5];
extern RwUInt8 _miscData[0x10];
extern RwaUUID rwaSTREAMSOURCEID;
extern RwaUUID rwaSTREAMSOURCEINTERFACEID;
extern RwaUUID rwaSTREAMRENDERERINTERFACEID;
extern RwaObjInterfaceDef D_01E81020;      // the stream source interface definition
extern RwaObjInterfaceDef D_01E81050;      // the stream renderer interface definition
extern RwaObjInterfaceDefParam _rwaStreamSourceIntInputs[5];
extern RwaObjInterfaceDefParam _rwaStreamSourceIntOutputs[5];
extern RwaObjInterfaceDefParam D_0049F250[5];
extern RwaObjInterfaceDefParam D_0049F2C0[6];
extern RwUInt8 D_01E7DF70[2 * 0x1804];     // two static 0x1800-byte blocks, each followed by its in-use word
extern const char D_004C17B0[];            // "no free block" message
extern const char D_004C17E0[];            // "block too big" message
extern const char D_004C1798[];            // a file extension compared with the stream name [G]
extern const char D_004C17A0[];            // the other one [G]
extern "C" int printf(const char *fmt, ...);
extern "C" RwInt32 DeleteSema(RwInt32 id);
extern "C" RwInt32 sceSifFreeSysMemory(RwUInt32 addr);
extern "C" void func_00293450(void *p);
extern "C" RwChar *func_002934D0(const RwChar *s);
extern "C" RwaUniqueID *_rwaUniqueIDAssignUUID(RwaUniqueID *id, RwaUUID *uuid);
extern "C" RwaUniqueID *_rwaUniqueIDAssignName(RwaUniqueID *id, const RwChar *name);
extern "C" void func_002A19F0(void *reader);
extern "C" RwInt32 _rwaStreamFileRequestQueueDestroy(void *q);
extern "C" void *_rwaStreamFileRequestQueueCreate(void *q, RwUInt32 *requests, RwUInt32 n);
extern "C" RwInt32 *RwaSemaphoreCreate(RwInt32 *sema, RwInt32 init, RwInt32 max);
extern "C" RwUInt32 *_rwaRwStreamOpen(RwUInt32 *stream, void *reader, RwUInt32 type, RwUInt32 access, const char *name,
                                      UnkFn_002A9750 cb, RwUInt32 arg);
extern "C" RwaObj *_RwaStreamNodeCreate(RwaObj *obj, RwaObj *p1, void *params);
extern "C" RwaObjInterfaceDef *RwaObjInterfaceDefFindByUUID(RwaUUID *id);
extern "C" RwaObjInterface *RwaObjInterfaceAttach(RwaObjInterfaceDef *intf, RwaObjDef *def, RwUInt32 p2, RwUInt32 p3);
extern "C" RwaObjInterfaceDef *RwaObjInterfaceDefCreate(RwaObjInterfaceDef *def);
extern "C" RwaObjInterfaceDef *RwaObjInterfaceDefAssignID(RwaObjInterfaceDef *def, RwaUUID *id, const char *name);
extern "C" void *RwaObjInterfaceDefSetup(RwaObjInterfaceDef *def, RwaObjInterfaceDefParam *inputs, RwUInt32 nInputs,
                                         RwaObjInterfaceDefParam *outputs, RwUInt32 nOutputs);
extern "C" RwaObjDef *RwaObjDefCreate(RwaObjDef *parent, RwUInt32 p1, RwaObjDef *def, void *intfInfo, void *aliasInfo,
                                      RwUInt32 hint);
extern "C" RwaObjDef *RwaObjDefAssignID(RwaObjDef *def, RwaUUID *id, const char *name);
extern "C" RwaObjDef *RwaObjDefSetupParams(RwaObjDef *def, RwaInputParamDef *inputs, RwUInt32 nInputs,
                                       RwaOutputParamDef *outputs, RwUInt32 nOutputs);
extern "C" RwaObjDef *RwaObjDefDestroy(RwaObjDef *def);
extern "C" RwaObjDef *RwaObjDefSetInterface(RwaObjDef *def, RwaObjInterfaceDef *intfDef);
typedef RwInt32 (*UnkFn_002AAF9C)(const RwChar *a, const char *b);

// Frees what the source owns (buffers it did not get from its creator: flags 1 / 2 / 0x200000 / 4), the description,
// the semaphore and the request queue; once (flag 0x2000).
// 0x002AABB0
extern "C" void _RwaStreamSourceFree(RwUInt8 *s)
{
    if (func_002A7630_inl1(s, 0x1A0) & 0x2000)
        return;
    if (func_002A7630_inl1(s, 0x1A0) & 8)
        func_002A19F0(s + 0x28);
    if (func_002A7630_inl1(s, 0x150) && !(func_002A7630_inl1(s, 0x1A0) & 1)) {
        if (func_002A7630_inl1(s, 0x18C) == 2)
            sceSifFreeSysMemory(func_002A7630_inl1(s, 0x150));
        else
            func_00293450((void *)func_002A7630_inl1(s, 0x150));
        func_002A7630_inl1(s, 0x150) = 0;
    }
    if (func_002A7630_inl1(s, 0x174) && !(func_002A7630_inl1(s, 0x1A0) & 2)) {
        if (func_002A7630_inl1(s, 0x18C) == 2)
            sceSifFreeSysMemory(func_002A7630_inl1(s, 0x174));
        else
            func_00293450((void *)func_002A7630_inl1(s, 0x174));
        func_002A7630_inl1(s, 0x174) = 0;
    }
    if (func_002A7630_inl1(s, 0x154) && !(func_002A7630_inl1(s, 0x1A0) & 0x200000))
        func_00293440((void *)func_002A7630_inl1(s, 0x154));
    if (func_002A7630_inl1(s, 0x18C) == 2 && func_002A7630_inl1(s, 0x178))
        func_00293440((void *)func_002A7630_inl1(s, 0x178));
    if (func_002A7630_inl1(s, 0x194)) {
        if (func_002A7630_inl1(s, 0x1A0) & 4)
            sceSifFreeSysMemory(func_002A7630_inl1(s, 0x194));
        func_002A7630_inl1(s, 0x194) = 0;
    }
    if (func_002A7630_inl1(s, 0x190)) {
        func_00293450((void *)func_002A7630_inl1(s, 0x190));
        func_002A7630_inl1(s, 0x190) = 0;
    }
    if (func_002A7630_inl1(s, 0x124)) {
        (*(UnkFn_002AA66C *)(s + 0x114))(func_002A7630_inl1(s, 0x124), func_002A7630_inl1(s, 0x118));
        func_002A7630_inl1(s, 0x124) = 0;
    }
    if ((RwInt32)func_002A7630_inl1(s, 0x1A4) > 0)
        DeleteSema(func_002A7630_inl1(s, 0x1A4));
    _rwaStreamFileRequestQueueDestroy(s + 0x98);
    func_002A7630_inl1(s, 0x1A0) |= 0x2000;
}

// 0x002AABA0
extern "C" void __rwaStreamSourceDestroy(RwUInt8 *s)
{
    _RwaStreamSourceFree(s);
}

extern "C" void func_002AB070(RwUInt32 p, RwUInt32 arg);
extern "C" RwUInt32 func_002AB0D0(RwUInt32 size, RwUInt32 arg);

// The source from its creation parameters (words): [0] / [1] / [2] the description allocator {alloc, free, arg}
// (defaults: the two static blocks), [4] the file name, [5] the source kind (1 file; 2 / 3 refused), [6] / [7] the
// wanted UUID / name, [8] / [9] blocks kept free / per read, [0xA] / [0xB] ring bytes / buffer, [0xC] block entries,
// [0xD] / [0xE] / [0xF] second buffer blocks / bytes / buffer, [0x10] 2 = IOP stream, [0x11] / [0x12] the owner's
// callback / argument. A file source opens its name as an RwStream when its extension is one of two [G].
// 0x002AAD70
extern "C" RwBool __rwaStreamSourceCreate(RwUInt8 *s, RwUInt32 *p)
{
    func_002A7630_inl1(s, 0x1A0) = 0;
    func_002A7630_inl1(s, 0) = 0;
    func_002A7630_inl1(s, 0xFC) = 0;
    func_002A7630_inl1(s, 0x100) = 0;
    func_002A7630_inl1(s, 0x104) = 0;
    func_002A7630_inl1(s, 0x108) = 0;
    func_002A7630_inl1(s, 0x124) = 0;
    func_002A7630_inl1(s, 0x110) = p[0];
    func_002A7630_inl1(s, 0x114) = p[1];
    func_002A7630_inl1(s, 0x118) = p[2];
    func_002A7630_inl1(s, 0x11C) = p[0x11];
    func_002A7630_inl1(s, 0x120) = p[0x12];
    func_002A7630_inl1(s, 0x19C) = p[8];
    func_002A7630_inl1(s, 0x198) = p[9];
    _rwaUniqueIDInitialize((RwaUniqueID *)(s + 0x13C));
    _rwaUniqueIDAssignName((RwaUniqueID *)(s + 0x13C), (const RwChar *)p[7]);
    _rwaUniqueIDAssignUUID((RwaUniqueID *)(s + 0x13C), (RwaUUID *)p[6]);
    func_002A7630_inl1(s, 0x174) = p[0xF];
    func_002A7630_inl1(s, 0x17C) = p[0xE];
    func_002A7630_inl1(s, 0x150) = p[0xB];
    func_002A7630_inl1(s, 0x16C) = p[0xA];
    func_002A7630_inl1(s, 0x160) = 0;
    func_002A7630_inl1(s, 0x164) = 0;
    func_002A7630_inl1(s, 0x15C) = 0;
    func_002A7630_inl1(s, 0x158) = 0;
    func_002A7630_inl1(s, 0x168) = 0;
    func_002A7630_inl1(s, 0x154) = p[0xC];
    func_002A7630_inl1(s, 0x184) = 0;
    func_002A7630_inl1(s, 0x180) = 0;
    func_002A7630_inl1(s, 0xF8) = 0;
    func_002A7630_inl1(s, 0x188) = p[0xD];
    func_002A7630_inl1(s, 0x178) = 0;
    func_002A7630_inl1(s, 0x18C) = p[0x10];
    func_002A7630_inl1(s, 0x190) = 0;
    func_002A7630_inl1(s, 0x194) = 0;
    if (!p[0])
        p[0] = (unsigned int)func_002AB0D0;
    if (!p[1])
        p[1] = (unsigned int)func_002AB070;
    if (!_rwaStreamFileRequestQueueCreate(s + 0x98, (RwUInt32 *)(s + 0xA8), 3))
        return 0;
    if (func_002A7630_inl1(s, 0x150))
        func_002A7630_inl1(s, 0x1A0) |= 1;
    if (func_002A7630_inl1(s, 0x174))
        func_002A7630_inl1(s, 0x1A0) |= 2;
    if (func_002A7630_inl1(s, 0x154))
        func_002A7630_inl1(s, 0x1A0) |= 0x200000;
    if (!RwaSemaphoreCreate((RwInt32 *)(s + 0x1A4), 1, 1)) {
        _RwaStreamSourceFree(s);
        return 0;
    }
    if (p[5] != 1)
        return 0;
    RwChar *ext = func_002934D0((const RwChar *)p[4]);
    func_002A7630_inl1(s, 0x1A0) |= 0x20;
    func_002A7630_inl1(s, 0x8C) = 0x80D;
    if ((*(UnkFn_002AAF9C *)(ourGlobals + 0x11C))(ext, D_004C1798) &&
        (*(UnkFn_002AAF9C *)(ourGlobals + 0x11C))(ext, D_004C17A0))
        return 0;
    _rwaRwStreamOpen((RwUInt32 *)(s + 4), s + 0x28, 2, 1, (const char *)p[4],
                     (UnkFn_002A9750)_RwaStreamSourceFileOpCallback, (unsigned int)s);
    return 1;
}

// The default description allocator's free: the static block's in-use word cleared.
// 0x002AB070
extern "C" void func_002AB070(RwUInt32 p, RwUInt32 arg)
{
    for (RwInt32 i = 0; i < 2; i++) {
        if (p == (unsigned int)(D_01E7DF70 + i * 0x1804)) {
            *(RwUInt32 *)(D_01E7DF70 + i * 0x1804 + 0x1800) = 0;
            return;
        }
    }
}

// The default description allocator: a free static block (a message when size > 0x1800, which is still handed out),
// else 0 with a message.
// 0x002AB0D0
extern "C" RwUInt32 func_002AB0D0(RwUInt32 size, RwUInt32 arg)
{
    if (!(size < 0x1801))
        printf(D_004C17E0);
    for (RwInt32 i = 0; i < 2; i++) {
        RwUInt8 *b = D_01E7DF70 + i * 0x1804;
        if (!*(RwUInt32 *)(b + 0x1800)) {
            *(RwUInt32 *)(b + 0x1800) = 1;
            return (unsigned int)b;
        }
    }
    printf(D_004C17B0);
    return 0;
}

// The source node: its creation parameters (from params word 7 on) re-packed for __rwaStreamSourceCreate with the
// completion callback _RwaStreamSourceOpenFunc(obj), the packet queue reset, out of any list (+0x14), the node made,
// the source created and the stream source interface attached.
// 0x002A7C00
extern "C" RwaObj *_RwaStreamSourceCreate(RwaObj *obj, RwaObj *p1, RwUInt32 *params)
{
    RwUInt32 c[19];
    for (RwUInt32 i = 0; i < 17; i++)
        c[i] = params[7 + i];
    c[17] = (unsigned int)_RwaStreamSourceOpenFunc;
    c[18] = (unsigned int)obj;
    RwaStreamPacketQueueInitialize((RwUInt8 *)obj + 0x228, 0, 0);
    if (func_002A7630_inl1(obj, 0x14)) {
        *(RwUInt32 *)func_002A7630_inl1(obj, 0x18) = func_002A7630_inl1(obj, 0x14);
        ((RwUInt32 *)func_002A7630_inl1(obj, 0x14))[1] = func_002A7630_inl1(obj, 0x18);
        func_002A7630_inl1(obj, 0x18) = 0;
        func_002A7630_inl1(obj, 0x14) = 0;
    }
    if (!_RwaStreamNodeCreate(obj, p1, params))
        return 0;
    if (!__rwaStreamSourceCreate((RwUInt8 *)obj + 0x74, c))
        return 0;
    func_002A7630_inl1(obj, 0x70) = (unsigned int)RwaObjInterfaceAttach(
        RwaObjInterfaceDefFindByUUID(&rwaSTREAMSOURCEINTERFACEID), obj->definition, params[5], params[6]);
    return obj;
}

// 0x002A7DC0
extern "C" void _RwaStreamSourceRegisterCommon(RwaObjDef *def)
{
    RwaObjDefAssignID(def, &rwaSTREAMSOURCEID, 0);
    func_00297C30_inl3(def, 0x200, 0x40);
    def->createFunc = _RwaStreamSourceCreate;
    def->destroyFunc = _RwaStreamSourceDestroy;
    def->startExecuteFunc = _RwaStreamSourceStartExecute;
    def->getParamsFunc = _RwaStreamSourceGetParams;
    def->createParamSize = 0x60;
    def->miscData = _miscData;
    def->noFreeListEntries = 2;
}

// 0x002AD140
extern "C" RwaObjInterfaceDef *RwaStreamSourceInterfaceRegister(void)
{
    if (!RwaObjInterfaceDefCreate(&D_01E81020))
        return 0;
    RwaObjInterfaceDefAssignID(&D_01E81020, &rwaSTREAMSOURCEINTERFACEID, 0);
    RwaObjInterfaceDefSetup(&D_01E81020, _rwaStreamSourceIntInputs, 5, _rwaStreamSourceIntOutputs, 5);
    return &D_01E81020;
}

// The stream renderer interface definition.
// 0x002AD1C0
extern "C" RwaObjInterfaceDef *func_002AD1C0(void)
{
    if (!RwaObjInterfaceDefCreate(&D_01E81050))
        return 0;
    RwaObjInterfaceDefAssignID(&D_01E81050, &rwaSTREAMRENDERERINTERFACEID, 0);
    RwaObjInterfaceDefSetup(&D_01E81050, D_0049F250, 5, D_0049F2C0, 6);
    return &D_01E81050;
}

// 0x002A80B0
extern "C" RwaObjDef *RwaStreamSourceRegister(void)
{
    RwaObjDef *def = RwaObjDefCreate(_rwaGnullObjDef, 0xC, &D_01E7DF10, &D_01E7DEF0, &_streamsourceAliasInfo, 0x3080F);
    if (!def)
        return 0;
    _RwaStreamSourceRegisterCommon(def);
    if (!RwaObjDefSetupParams(def, _streamsourceInputParams, 5, _streamsourceOutputParams, 5)) {
        RwaObjDefDestroy(def);
        return 0;
    }
    RwaObjInterfaceDef *intf = RwaStreamSourceInterfaceRegister();
    if (!intf) {
        RwaObjDefDestroy(def);
        return 0;
    }
    RwaObjDefSetInterface(def, intf);
    return def;
}

// ---- additional functions ----
extern "C" RwBool RwaObjInterfaceDefDestroy(RwaObjInterfaceDef *def);
extern "C" void _RwaSPU2WaveIOPReinterleave(RwUInt32 records, RwUInt32 arg, RwUInt32 count);
extern "C" RwaObjInterfaceDef *RwaStreamSourceInterfaceUnregister(void);

// divu as the EE does it when the divisor is 0: quotient 0xFFFFFFFF [L: the R5900 rule; C's / would be undefined].
static RwUInt32 func_002A8FC0_inl1(RwUInt32 a, RwUInt32 b)
{
    return b ? a / b : 0xFFFFFFFF;
}

// `v` rounded up to a multiple of `unit` (retail: (v + unit - 1) / unit * unit, the multiply 32 bits).
static RwUInt32 func_002A8FC0_inl2(RwUInt32 v, RwUInt32 unit)
{
    return unit * func_002A8FC0_inl1(v + unit - 1, unit);
}

// Reinterleave: copies ready blocks of the ring (+0x150) into the second buffer (+0x174), format by format, one block
// entry at a time, as long as the entry at the lock tail (+0x168) is ready (state flag 2) and the destination entry has
// not been completed. Per format the data is moved in frames: `unit` bytes (the format's +0x1A) per copy step of the widest
// element size dividing it (2 bytes when even, else bytes; the 4 / 8 / 16-byte steps are unreachable as retail tests
// bit 0 first [C 0x2A914C-0x2A9188]), channel by channel, each with a frame size (+0x18) of source and destination.
// With an IOP stream (+0x18C == 2) the copies are not made: a 0x24-byte record per format is written to +0x190 and
// handed to _RwaSPU2WaveIOPReinterleave at the end. The result of retail's function is the destination buffer address
// of a completed entry (frame +0x19C); the caller does not read it.
// 0x002A8FC0
extern "C" void _RwaStreamSourceReinterleave(RwUInt8 *s)
{
    B3_RETAIL_FRAME(0x1B0);
    RwUInt32 *mulLo = B3_FRAME_LOCAL(RwUInt32, 0x1A0);      // RwaMul32By32's outputs
    RwUInt32 *mulHiDst = B3_FRAME_LOCAL(RwUInt32, 0x1A4);
    RwUInt32 *mulHiSrc = B3_FRAME_LOCAL(RwUInt32, 0x1A8);
    RwUInt32 *mulHiEnd = B3_FRAME_LOCAL(RwUInt32, 0x1AC);
    RwUInt32 completed = 0;       // frame +0x19C
    RwUInt32 nrec = 0;            // frame +0x180: IOP records written
    RwUInt8 *desc = (RwUInt8 *)func_002A7630_inl1(s, 0x124);
    RwUInt32 bsz = func_002A7630_inl1(desc, 0x20);
    RwUInt32 nfmt = func_002A7630_inl1(desc, 0x14);
    while (completed == 0) {
        if (!(func_002A87B0_inl3(s, 0x154, func_002A7630_inl1(s, 0x168), 0x10) & 2))
            break;
        RwUInt32 lastFmt = nfmt - 1;
        RwUInt32 bufA = func_002A7630_inl1(s, 0x150) + func_002A7630_inl1(s, 0x168) * bsz;
        RwUInt32 bufB = func_002A7630_inl1(s, 0x174) + func_002A7630_inl1(s, 0x180) * bsz;
        RwUInt32 *src = (RwUInt32 *)(func_002A7630_inl1(s, 0x154) + func_002A7630_inl1(s, 0x168) * 20);
        RwUInt32 *dst = (RwUInt32 *)(func_002A7630_inl1(s, 0x178) + func_002A7630_inl1(s, 0x180) * 20);
        RwUInt32 srcChange = src[3];
        RwUInt32 dstChange = dst[3];
        RwUInt32 srcDone = 0;     // frame +0x120
        RwUInt32 dstDone = 0;     // frame +0x110
        RwUInt32 fp = 0;          // retail's $fp: the bytes copied for the format
        RwUInt32 s2 = 0;          // retail's $s2: the format's block bytes
        for (RwUInt32 i = 0; i < nfmt; i++) {
            RwUInt8 *fmt = (RwUInt8 *)(func_002A7630_inl1(desc, 0x18) + i * 40);
            RwUInt32 w1C = *(RwUInt16 *)(fmt + 0x1C);
            RwUInt32 w18 = *(RwUInt16 *)(fmt + 0x18);
            RwUInt32 unit = *(RwUInt16 *)(fmt + 0x1A);
            RwUInt32 channels = ((RwUInt8 *)func_002A7630_inl1(fmt, 0x14))[0xD];
            RwUInt32 step;      // copy element: 1 byte, 2 / 4 / 8 / 16 bytes (retail's $s1 = 1 .. 5)
            RwUInt32 count;     // elements per unit (retail's $s5)
            if (!(unit & 1)) {
                step = 2;
                count = unit >> 1;
            } else if (!(unit & 3)) {
                step = 3;
                count = unit >> 2;
            } else if (!(unit & 7)) {
                step = 4;
                count = unit >> 3;
            } else if (unit & 0xF) {
                step = 1;
                count = unit;
            } else {
                step = 5;
                count = unit >> 4;
            }
            s2 = w1C * w18;
            RwaMul32By32(srcChange, s2, mulHiSrc, mulLo);
            RwaMul32By32(dstChange, s2, mulHiDst, mulLo);
            *mulHiEnd = s2;
            RwUInt32 end;
            if (src[4] & 0x10) {
                RwaMul32By32(func_002A7630_inl1(s, 0xF8), s2, mulHiEnd, mulLo);
                end = func_002A8FC0_inl2(*mulHiEnd, unit);
                *mulHiEnd = end;
            } else {
                end = s2;
            }
            RwUInt32 srcR = func_002A8FC0_inl2(*mulHiSrc, *(RwUInt16 *)(fmt + 0x1A));
            RwUInt32 dstHi = *mulHiDst;
            *mulHiSrc = srcR;
            RwUInt32 dstR = func_002A8FC0_inl2(dstHi, *(RwUInt16 *)(fmt + 0x1A));
            RwUInt32 room = end - srcR;
            fp = s2 - dstR;
            *mulHiDst = dstR;
            if (!(fp < room))
                fp = room;
            if (lastFmt == i) {
                srcDone = end == srcR + fp;
                dstDone = s2 == dstR + fp;
            }
            RwUInt32 srcQ = func_002A8FC0_inl1(srcR, w18);
            RwUInt32 srcBase = (srcQ & 0xFFFF) * w18;
            RwUInt32 srcRem = (srcR - srcBase) & 0xFFFF;
            RwUInt32 dstQ = func_002A8FC0_inl1(dstR, w18);
            RwUInt32 dstBase = (dstQ & 0xFFFF) * w18;
            RwUInt32 dstRem = dstR - dstBase;
            RwUInt32 skip = w18 * (channels - 1);
            RwUInt32 srcPtr = bufA + func_002A7630_inl1(fmt, 0x24) + srcRem;
            RwUInt32 dstPtr = bufB + func_002A7630_inl1(fmt, 0x24) + dstRem + channels * dstBase;
            srcPtr = srcPtr + channels * srcBase;
            if (func_002A7630_inl1(s, 0x18C) == 2) {
                RwUInt32 *rec = (RwUInt32 *)(func_002A7630_inl1(s, 0x190) + nrec * 36);
                nrec++;
                rec[0] = channels;
                rec[1] = srcPtr;
                rec[2] = dstPtr;
                rec[3] = w18;
                rec[5] = srcRem;
                rec[6] = dstRem;
                rec[7] = fp;
                rec[8] = unit;
            } else {
                RwUInt32 srcLeft0 = (w18 - srcRem) & 0xFFFF;
                RwUInt32 dstLeft0 = (w18 - dstRem) & 0xFFFF;
                RwUInt32 n0 = fp & 0xFFFF;
                for (RwUInt32 ch = 0; (ch & 0xFF) < (channels & 0xFF); ch = (ch + 1) & 0xFF) {
                    RwUInt32 a3 = srcPtr;
                    RwUInt32 a2 = dstPtr;
                    RwUInt32 srcLeft = srcLeft0;
                    RwUInt32 dstLeft = dstLeft0;
                    RwUInt32 left = n0;
                    while (left != 0) {
                        if ((srcLeft & 0xFFFF) == 0) {
                            srcLeft = w18;
                            a3 += skip;
                        }
                        if ((dstLeft & 0xFFFF) == 0) {
                            dstLeft = w18;
                            a2 += skip;
                        }
                        if (step == 5) {
                            for (RwUInt32 k = 0; (k & 0xFFFF) < count; k = (k + 1) & 0xFFFF) {
                                unsigned long long lo = *(unsigned long long *)a3;
                                unsigned long long hi = *(unsigned long long *)(a3 + 8);
                                *(unsigned long long *)a2 = lo;
                                *(unsigned long long *)(a2 + 8) = hi;
                                a3 += 16;
                                a2 += 16;
                            }
                            srcLeft -= unit;
                        } else if (step == 4) {
                            for (RwUInt32 k = 0; (k & 0xFFFF) < count; k = (k + 1) & 0xFFFF) {
                                *(unsigned long long *)a2 = *(unsigned long long *)a3;
                                a3 += 8;
                                a2 += 8;
                            }
                            srcLeft -= unit;
                        } else if (step == 3) {
                            for (RwUInt32 k = 0; (k & 0xFFFF) < count; k = (k + 1) & 0xFFFF) {
                                *(RwUInt32 *)a2 = *(RwUInt32 *)a3;
                                a3 += 4;
                                a2 += 4;
                            }
                            srcLeft -= unit;
                        } else if (step == 2) {
                            for (RwUInt32 k = 0; (k & 0xFFFF) < count; k = (k + 1) & 0xFFFF) {
                                *(RwUInt16 *)a2 = *(RwUInt16 *)a3;
                                a3 += 2;
                                a2 += 2;
                            }
                            srcLeft -= unit;
                        } else if (step == 1) {
                            for (RwUInt32 k = 0; (k & 0xFFFF) < count; k = (k + 1) & 0xFFFF) {
                                *(RwUInt8 *)a2 = *(RwUInt8 *)a3;
                                a3 += 1;
                                a2 += 1;
                            }
                            srcLeft -= unit;
                        } else {
                            srcLeft -= unit;
                        }
                        dstLeft -= unit;
                        left -= unit;
                        srcLeft &= 0xFFFF;
                        dstLeft &= 0xFFFF;
                        left &= 0xFFFF;
                    }
                    srcPtr += w18;
                    dstPtr += w18;
                }
            }
            if (lastFmt == i)
                break;
        }
        // the destination entry takes the source entry's segment and block, the source's value word
        dst[0] = src[0];
        dst[1] = src[1];
        dst[2] = func_002A7630_inl1(s, 0xFC);
        if (dstDone) {
            dst[3] = 0;
            dst[4] = 2;
            completed = bufB;
            func_002A7630_inl1(s, 0x180) = func_002A7630_inl1(s, 0x180) + 1;
            if (func_002A7630_inl1(s, 0x180) == func_002A7630_inl1(s, 0x188))
                func_002A7630_inl1(s, 0x180) = 0;
        } else {
            RwaMul32By32(*mulHiDst + fp, func_002A8FC0_inl1(0xFFFFFFFF, s2), mulLo, &dst[3]);
            dst[3] = dst[3] + 1;
        }
        if (srcDone) {
            if (_rwaGprogramContext == 2) {
                WaitSema(func_002A7630_inl1(s, 0x1A4));
                src[3] = 0;
            } else {
                src[3] = 0;
            }
            src[4] = 1;
            RwUInt32 tail = func_002A7630_inl1(s, 0x168);
            RwUInt32 next = tail + 1;
            if (next == func_002A7630_inl1(s, 0x170))
                next = 0;
            func_002A7630_inl1(s, 0x168) = next;
            if (func_002A7630_inl1(s, 0x160) == tail) {
                func_002A7630_inl1(s, 0x160) = next;
                func_002A7630_inl1(s, 0x164) = next;
            }
            if (_rwaGprogramContext == 2)
                SignalSema(func_002A7630_inl1(s, 0x1A4));
        } else {
            RwaMul32By32(*mulHiSrc + fp, func_002A8FC0_inl1(0xFFFFFFFF, s2), mulLo, &src[3]);
            src[3] = src[3] + 1;
        }
    }
    if (func_002A7630_inl1(s, 0x18C) == 2 && nrec)
        _RwaSPU2WaveIOPReinterleave(func_002A7630_inl1(s, 0x190), func_002A7630_inl1(s, 0x194), nrec);
}

// Flag 0x8000 of the source into *out (the getter of func_002A7390).
// 0x002A7370
extern "C" RwaObj *func_002A7370(RwaObj *obj, RwUInt32 param, RwUInt32 *out)
{
    *out = func_002A7630_inl1(obj, 0x214) & 0x8000;
    return obj;
}

// The stream position: pos = {segment, kind, sample position, units}. The current block entry (0x14 bytes, array +0x1C8,
// index +0x1D0)'s segment record gives the segment: kind 1 its name word (+4), kind 2 its UUID word (+0),
// otherwise the kind becomes 3 and the segment is its index in the description's segment records (0x20 each). The sample
// position is GetSamplePosUsingPos of the entry; units = 2.
// 0x002A73D0
extern "C" RwaObj *_RwaStreamSourceGetStreamPos(RwaObj *obj, RwUInt32 param, RwUInt32 *pos)
{
    RwUInt32 idx = func_002A7630_inl1(obj, 0x1D0);
    RwUInt32 base = func_002A7630_inl1(obj, 0x1C8);
    RwUInt32 kind = pos[1];
    RwUInt32 *entry = (RwUInt32 *)(base + idx * 20);
    if (kind == 1) {
        pos[0] = ((RwUInt32 *)entry[0])[1];
    } else if (kind == 2) {
        pos[0] = ((RwUInt32 *)entry[0])[0];
    } else {
        pos[1] = 3;
        RwUInt32 segs = func_002A7630_inl1((void *)func_002A7630_inl1((RwUInt8 *)obj + 0x74, 0x124), 0x10);
        pos[0] = (entry[0] - segs) >> 5;
    }
    pos[3] = 2;
    pos[2] = __rwaStreamSourceGetSamplePosUsingPos((RwUInt8 *)obj + 0x74, entry, 0);
    return obj;
}

// Destroys the interface definition and, if that worked, the object definition.
// 0x002A7D80
extern "C" RwaObjDef *RwaStreamSourceUnregister(void)
{
    if (!RwaStreamSourceInterfaceUnregister())
        return 0;
    return RwaObjDefDestroy(&D_01E7DF10);
}

// The segment (index) whose name compares equal (ourGlobals +0x114, 0 = equal) to `name`; 0 when none. `s` = the source
// (obj + 0x74); the description's segment count is +0xC, the records +0x10, 0x20 each, the name +4 (skipped when null).
// 0x002A9A60
extern "C" RwUInt32 __rwaStreamSourceGetSegmentIndexByName(RwUInt8 *s, RwUInt32 name)
{
    for (RwUInt32 i = 0; i < func_002A7630_inl1((void *)func_002A7630_inl1(s, 0x124), 0xC); i++) {
        RwUInt32 seg = func_002A7630_inl1((void *)func_002A7630_inl1(s, 0x124), 0x10) + i * 32;
        RwUInt32 n = func_002A7630_inl1((void *)seg, 4);
        if (n && !(*(UnkFn_002AA5B8 *)(ourGlobals + 0x114))(n, name))
            return i;
    }
    return 0;
}

// The same by UUID (RwaUUIDCompare, record +0).
// 0x002A9B00
extern "C" RwUInt32 __rwaStreamSourceGetSegmentIndexByUUID(RwUInt8 *s, RwUInt32 uuid)
{
    for (RwUInt32 i = 0; i < func_002A7630_inl1((void *)func_002A7630_inl1(s, 0x124), 0xC); i++) {
        RwUInt32 seg = func_002A7630_inl1((void *)func_002A7630_inl1(s, 0x124), 0x10) + i * 32;
        RwUInt32 u = func_002A7630_inl1((void *)seg, 0);
        if (u && !RwaUUIDCompare((const RwaUUID *)u, (const RwaUUID *)uuid))
            return i;
    }
    return 0;
}

// Selects the segment record (+0x14C) with that name; segment 0 when none matches.
// 0x002A9B90
extern "C" void __rwaStreamSourceSetSegmentByName(RwUInt8 *s, RwUInt32 name)
{
    RwUInt32 i = 0;
    for (; i < func_002A7630_inl1((void *)func_002A7630_inl1(s, 0x124), 0xC); i++) {
        RwUInt32 seg = func_002A7630_inl1((void *)func_002A7630_inl1(s, 0x124), 0x10) + i * 32;
        RwUInt32 n = func_002A7630_inl1((void *)seg, 4);
        if (n && !(*(UnkFn_002AA5B8 *)(ourGlobals + 0x114))(n, name))
            goto found;
    }
    i = 0;
found:
    func_002A7630_inl1(s, 0x14C) = func_002A7630_inl1((void *)func_002A7630_inl1(s, 0x124), 0x10) + i * 32;
}

// The same by UUID.
// 0x002A9C40
extern "C" void __rwaStreamSourceSetSegmentByUUID(RwUInt8 *s, RwUInt32 uuid)
{
    RwUInt32 i = 0;
    for (; i < func_002A7630_inl1((void *)func_002A7630_inl1(s, 0x124), 0xC); i++) {
        RwUInt32 seg = func_002A7630_inl1((void *)func_002A7630_inl1(s, 0x124), 0x10) + i * 32;
        RwUInt32 u = func_002A7630_inl1((void *)seg, 0);
        if (u && !RwaUUIDCompare((const RwaUUID *)u, (const RwaUUID *)uuid))
            goto found;
    }
    i = 0;
found:
    func_002A7630_inl1(s, 0x14C) = func_002A7630_inl1((void *)func_002A7630_inl1(s, 0x124), 0x10) + i * 32;
}

// The creation parameters' defaults: [0] / [1] the description allocator {func_002AB0D0, func_002AB070}, the rest 0.
// 0x002AB010
extern "C" void __rwaStreamSourceCreationParamsInit(RwUInt32 *params)
{
    params[0] = (unsigned int)func_002AB0D0;
    params[1] = (unsigned int)func_002AB070;
    params[2] = 0;
    params[0x44 / 4] = 0;
    params[0x48 / 4] = 0;
    params[0x10 / 4] = 0;
    params[0x18 / 4] = 0;
    params[0x1C / 4] = 0;
    params[0x14 / 4] = 0;
    params[0x28 / 4] = 0;
    params[0x2C / 4] = 0;
    params[0x30 / 4] = 0;
    params[0x38 / 4] = 0;
    params[0x3C / 4] = 0;
    params[0x20 / 4] = 0;
    params[0x24 / 4] = 0;
    params[0x34 / 4] = 0;
    params[0xC / 4] = 0;
}

// 0x002AD130
extern "C" RwaObjInterfaceDef *RwaStreamSourceInterfaceUnregister(void)
{
    return (RwaObjInterfaceDef *)RwaObjInterfaceDefDestroy(&D_01E81020);
}

// 0x002AD1B0
extern "C" RwaObjInterfaceDef *func_002AD1B0(void)
{
    return (RwaObjInterfaceDef *)RwaObjInterfaceDefDestroy(&D_01E81050);
}
