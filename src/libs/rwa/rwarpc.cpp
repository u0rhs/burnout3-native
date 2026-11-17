// rwarpc.o (librwaspu2.a) -- RWA's EE <-> IOP channel over SIF commands: every request is a 0x30-byte packet (SIF
// command 0, optionally with a DMA of data); the IOP's packets arrive through SIF command handler 1 into a 24-entry
// ring that an EE thread drains. Retail 0x2AEFB0-0x2AFA1C [C E map]. C port written from retail's code (audio1);
// proven by the checker on live captures where they exist (semaphore / thread code stays unchecked, see the report).
#include "rwa/rwacore.h"

// An RPC packet, 0x30 bytes [C RwaRPCTransfer 0x2AF18C-0x2AF1DC; _RwaRPCProcessSystemCommand]. The first 0x10 bytes
// are the SIF command header sceSifSendCmd fills.
struct UnkStruct_002AEFC0 {
    unsigned char field_0[0x10];  // 0x00 SIF command header
    RwUInt32 field_10;            // 0x10 opcode
    RwUInt32 field_14;            // 0x14 the EE thread to wake / callback to call (p5)
    RwUInt32 field_18;            // 0x18 EE address of the data
    RwUInt32 field_1C;            // 0x1C IOP address of the data
    RwUInt32 field_20;            // 0x20 data size (the whole transfer, every chunk)
    RwUInt32 field_24;            // 0x24 argument 0 (p6)
    RwUInt32 field_28;            // 0x28 argument 1 (p7)
    RwUInt32 field_2C;            // 0x2C 1 = last (or only) chunk
};
// A queued packet (IOP RAM busy), 0x40 bytes: the packet, then its DMA [C 0x2AF1EC-0x2AF204].
struct UnkStruct_01E811C0 {
    UnkStruct_002AEFC0 field_0;
    RwUInt32 field_30;            // 0x30 source (EE)
    RwUInt32 field_34;            // 0x34 destination (IOP)
    RwUInt32 field_38;            // 0x38 bytes
    unsigned char pad_3C[0x4];
};

typedef void (*RwaRPCCmdHandlerFunc)(void *userData, RwUInt32 op, RwUInt32 arg0, RwUInt32 arg1, RwInt32 thread);
typedef void (*UnkFn_002AF43C)(RwUInt32 arg0, RwUInt32 arg1);
typedef void (*UnkFn_002AF9E8)(void *pkt);
typedef void (*UnkFn_002AF6FC)(void);

extern RwUInt32 _RPCIOPRAMBusy;            // a transfer into IOP RAM is in flight: later ones queue
extern RwInt32 _RPCTransferSema;
extern RwInt32 _RPCIRQSema;
extern RwUInt32 _RPCQueueLast;
extern RwUInt32 _RPCQueueCurrent;
extern RwUInt32 _RPCSentCurrent;
extern RwInt32 _RPCReceivedCurrent;
extern RwInt32 _RPCReceivedLast;
extern RwUInt32 _rwaGRPCDMAWorkArea;       // the IOP's DMA work area (reply to opcode 2 = system 0xA)
extern RwUInt32 _rwaGRPCWaveRendererUploadData;
extern RwUInt32 _rwaGRwaSPU2RemoteIOPFopsDataIOPBuffer;
extern RwBool _RPCThreadRunning;
extern RwInt32 _rwaGprogramContext;        // 2 = threads (the update thread waits on _RPCIRQSema)
extern RwUInt32 _RPCVersion;               // the IOP module's version (reply to opcode 1 = system 9): 0x115
extern RwaRPCCmdHandlerFunc _RPCCmdHandler;
extern void *_RwaRPCUserData;
extern RwInt32 lastSifCmdId;
extern RwInt32 _rwaGoldThreadPriority;
extern RwBool _RPCOpen;
extern UnkStruct_01E811C0 D_01E811C0[24];  // the queue
extern UnkStruct_002AEFC0 D_01E817C0[24];  // the sent packets
extern UnkStruct_002AEFC0 D_01E81C40[24];  // the received packets
extern RwUInt8 D_01E828C0[0x20];           // the update thread's RwaThread [G size]
extern void *D_01E828D0;                   // its stack pointer field (D_01E828C0 + 0x10)
extern RwUInt8 D_01E820C0[0x800];          // its stack
extern RwUInt8 D_01E828E0[0x30 * 0x18];    // the SIF command buffer [G size: 0x18 entries]
extern RwInt8 D_004C1A90[0x48];                // "EE  RwaRPCTransfer : ERROR! DMA command queue FULL! Discarding o..."
extern "C" void *memcpy(void *dst, const void *src, unsigned int n);
extern "C" RwInt32 WaitSema(RwInt32 id);
extern "C" RwInt32 SignalSema(RwInt32 id);
extern "C" RwInt32 iSignalSema(RwInt32 id);
extern "C" RwInt32 DeleteSema(RwInt32 id);
extern "C" RwInt32 printf(const char *fmt, ...);
extern "C" RwUInt32 sceSifSendCmd(RwUInt32 cid, void *pkt, RwUInt32 psize, RwUInt32 src, RwUInt32 dst, RwUInt32 size);
extern "C" RwInt32 sceSifDmaStat(RwInt32 id);
extern "C" void sceSifInitCmd(void);
extern "C" void *sceSifSetCmdBuffer(void *buf, RwInt32 n);
extern "C" int sceSifAddCmdHandler(RwUInt32 cid, UnkFn_002AF9E8 handler, RwUInt32 data);
extern "C" int sceSifRemoveCmdHandler(RwUInt32 cid);
extern "C" RwInt32 GetThreadId(void);
extern "C" RwInt32 ChangeThreadPriority(RwInt32 id, RwInt32 prio);
extern "C" void FlushCache(RwInt32 mode);
extern "C" RwInt32 sceGsSyncV(RwInt32 mode);
extern "C" RwInt32 func_00114E60(void);    // DIntr
extern "C" RwInt32 EIntr(void);
extern "C" RwInt32 *RwaSemaphoreCreate(RwInt32 *sema, RwInt32 init, RwInt32 max);
extern "C" void *_RwaThreadCreate(void *thread, UnkFn_002AF6FC entry, RwUInt32 priority, RwUInt32 stackSize);
extern "C" void *_RwaThreadStart(void *thread, RwUInt32 arg);
extern "C" void RwaThreadTerminate(void *thread);
extern "C" RwBool _RwaRPCProcessSystemCommand(UnkStruct_002AEFC0 *pkt);

// 0x002AEFB0
extern "C" RwUInt32 RwaRPCDmaBusy(void)
{
    return _RPCIOPRAMBusy;
}

// Sends a request: opcode `op`, data (ee -> iop, `size` bytes) in chunks of at most 0xFFC00 bytes, one packet per
// chunk, each carrying the whole transfer's ee / iop / size, p5 and the two arguments. Data-less opcodes: 3, 5, 6, 8,
// 0x19. Opcode 4 always sends at once; 7 (read into the IOP DMA work area, chunks of 0x200000, only the first sent)
// and 8 queue while another such transfer is in flight (_RPCIOPRAMBusy); the queue holds 24 (the oldest is dropped
// with a message when it is full).
// 0x002AEFC0
extern "C" void RwaRPCTransfer(RwUInt32 iop, void *ee, RwUInt32 size, RwUInt32 op, RwUInt32 unused, RwUInt32 p5,
                               RwUInt32 p6, RwUInt32 p7)
{
    WaitSema(_RPCTransferSema);
    RwUInt32 eeAddr = (unsigned int)ee;
    RwUInt32 mode = 1;      // 1 send now, 2 queue
    RwUInt32 src = 0, dst = 0, left = 0, chunkMax = 0;
    switch (op) {
    case 0x19:
    case 5:
    case 3:
    case 6:
        break;
    case 8:
        if (!_RPCIOPRAMBusy)
            _RPCIOPRAMBusy = mode;
        else
            mode = 2;
        break;
    case 7:
        dst = _rwaGRPCDMAWorkArea;
        chunkMax = 0x200000;
        src = eeAddr;
        left = size < 0x200000 ? size : chunkMax;
        if (!_RPCIOPRAMBusy)
            _RPCIOPRAMBusy = 1;
        else
            mode = 2;
        break;
    default:
        dst = iop;
        src = eeAddr;
        left = size;
        chunkMax = 0xFFC00;
        break;
    }
    if (mode == 2 && !(_RPCQueueLast < _RPCQueueCurrent + 0x18)) {
        printf((const char *)D_004C1A90);
        _RPCQueueCurrent++;
    }
    RwUInt32 *counter = mode == 2 ? &_RPCQueueLast : &_RPCSentCurrent;
    do {
        RwUInt32 idx = *counter % 0x18;
        UnkStruct_002AEFC0 *pkt = mode == 2 ? &D_01E811C0[idx].field_0 : &D_01E817C0[idx];
        pkt->field_10 = op;
        pkt->field_18 = eeAddr;
        pkt->field_1C = iop;
        pkt->field_20 = size;
        pkt->field_24 = p6;
        pkt->field_28 = p7;
        pkt->field_14 = p5;
        RwUInt32 chunk = left < chunkMax ? left : chunkMax;
        pkt->field_2C = chunk == left ? 1 : 0;
        if (mode == 2) {
            D_01E811C0[idx].field_30 = src;
            D_01E811C0[idx].field_34 = dst;
            D_01E811C0[idx].field_38 = chunk;
        } else {
            sceSifSendCmd(0, pkt, 0x30, src, dst, chunk);
        }
        left -= chunk;
        src += chunk;
        dst += chunk;
        (*counter)++;
    } while (left);
    SignalSema(_RPCTransferSema);
}

// The update thread: drains the received ring (waiting on _RPCIRQSema when threads are used) until a command
// returns 0.
// 0x002AF2A0
extern "C" void _RwaRPCUpdateThread(void)
{
    _RPCThreadRunning = 1;
    while (_RPCThreadRunning) {
        if (_rwaGprogramContext == 2)
            WaitSema(_RPCIRQSema);
        if (_RPCThreadRunning) {
            _RPCThreadRunning = _RwaRPCProcessSystemCommand(&D_01E81C40[_RPCReceivedCurrent % 0x18]);
            _RPCReceivedCurrent++;
        }
    }
}

// SIF command handler 1 (interrupt context): the IOP's packet into the ring (dropped when 24 are pending).
// 0x002AF340
extern "C" void _RwaRPCSifIntHandler(void *pkt)
{
    if (_RPCReceivedLast < _RPCReceivedCurrent + 0x18) {
        RwInt32 idx = _RPCReceivedLast % 0x18;
        _RPCReceivedLast++;
        memcpy(&D_01E81C40[idx], pkt, 0x30);
        iSignalSema(_RPCIRQSema);
    }
}

// One packet from the IOP. System opcodes: 9 version (arg0); 0xA work areas (arg0 DMA work area, + 0x400 wave
// upload data; arg1 remote file buffer); 0xB-0xE a transfer's last chunk done (not 0xC: IOP RAM free again), then
// the packet's callback (arg0, arg1); 0xF a chunked read continues (opcode 7 again, next 0x200000) or, at its last
// chunk, completes (IOP RAM free, callback); 0x10 callback, IOP RAM free. Others go to the user handler (the SPU2
// output's RwaSPU2OutputCmdHandler). After a last chunk the next queued packet is sent unless IOP RAM is busy or
// the previous SIF DMA is still running. Returns 1.
// 0x002AF3C0
extern "C" RwBool _RwaRPCProcessSystemCommand(UnkStruct_002AEFC0 *pkt)
{
    RwUInt32 op = pkt->field_10;
    switch (op) {
    case 9:
        _RPCVersion = pkt->field_24;
        break;
    case 10:
        _rwaGRPCDMAWorkArea = pkt->field_24;
        _rwaGRPCWaveRendererUploadData = pkt->field_24 + 0x400;
        _rwaGRwaSPU2RemoteIOPFopsDataIOPBuffer = pkt->field_28;
        break;
    case 11:
    case 12:
    case 13:
    case 14:
        if (!pkt->field_2C)
            return 1;
        if (op != 12)
            _RPCIOPRAMBusy = 0;
        if (pkt->field_14)
            ((UnkFn_002AF43C)pkt->field_14)(pkt->field_24, pkt->field_28);
        break;
    case 15:
        if (pkt->field_2C) {
            _RPCIOPRAMBusy = 0;
            if (pkt->field_14)
                ((UnkFn_002AF43C)pkt->field_14)(pkt->field_24, pkt->field_28);
        } else {
            RwInt32 n = (RwInt32)pkt->field_20;
            if (!(n < 0x200000))
                n = 0x200000;
            pkt->field_10 = 7;
            sceSifSendCmd(0, pkt, 0x30, pkt->field_18, _rwaGRPCDMAWorkArea, n);
        }
        break;
    case 16:
        if (pkt->field_14)
            ((UnkFn_002AF43C)pkt->field_14)(pkt->field_24, pkt->field_28);
        _RPCIOPRAMBusy = 0;
        break;
    default:
        if (_RPCCmdHandler)
            _RPCCmdHandler(_RwaRPCUserData, pkt->field_10, pkt->field_24, pkt->field_28, (RwInt32)pkt->field_14);
        break;
    }
    if (!pkt->field_2C)
        return 1;
    if (_RPCIOPRAMBusy)
        return 1;
    if (!(_RPCQueueCurrent < _RPCQueueLast))
        return 1;
    UnkStruct_01E811C0 *q = &D_01E811C0[_RPCQueueCurrent % 0x18];
    if (q->field_0.field_10 == 7)
        _RPCIOPRAMBusy = 1;
    if (lastSifCmdId && sceSifDmaStat(lastSifCmdId) >= 0)
        return 1;
    lastSifCmdId = (RwInt32)sceSifSendCmd(0, &q->field_0, 0x30, q->field_30, q->field_34, q->field_38);
    _RPCQueueCurrent++;
    return 1;
}

// Stops the update thread (signalling it when it waits) and frees the semaphores.
static void func_002AF5F0_inl1(void)
{
    _rwaGRPCDMAWorkArea = 0;
    sceSifRemoveCmdHandler(1);
    _RPCThreadRunning = 0;
    if (_rwaGprogramContext == 2)
        SignalSema(_RPCIRQSema);
    if (_rwaGprogramContext == 2)
        WaitSema(_RPCIRQSema);
    RwaThreadTerminate(D_01E828C0);
}

// 0x002AF5F0
extern "C" void RwaRPCClose(void)
{
    func_002AF5F0_inl1();
    DeleteSema(_RPCIRQSema);
    DeleteSema(_RPCTransferSema);
    _RPCOpen = 0;
}

// 0x002AF670
extern "C" RwBool RwaRPCOpen(RwaRPCCmdHandlerFunc handler, void *userData, RwUInt32 priority)
{
    RwUInt32 tries = 0;
    _RPCCmdHandler = handler;
    _RwaRPCUserData = userData;
    _rwaGoldThreadPriority = ChangeThreadPriority(GetThreadId(), 0x32);
    _RPCReceivedCurrent = 0;
    _RPCReceivedLast = 0;
    if (!RwaSemaphoreCreate(&_RPCIRQSema, 0, 0x18))
        return 0;
    if (!RwaSemaphoreCreate(&_RPCTransferSema, 1, 0x18)) {
        DeleteSema(_RPCIRQSema);
        return 0;
    }
    D_01E828D0 = D_01E820C0;
    if (!_RwaThreadCreate(D_01E828C0, _RwaRPCUpdateThread, priority, 0x800)) {
        DeleteSema(_RPCTransferSema);
        DeleteSema(_RPCIRQSema);
        return 0;
    }
    if (!_RwaThreadStart(D_01E828C0, 0)) {
        RwaThreadTerminate(D_01E828C0);
        DeleteSema(_RPCTransferSema);
        DeleteSema(_RPCIRQSema);
        return 0;
    }
    _RPCOpen = 1;
    _RPCVersion = 0;
    RwaRPCTransfer(0, 0, 0, 1, 0, 0, 0, 0);
    while (!_RPCVersion && tries < 0x258) {
        FlushCache(0);
        sceGsSyncV(0);
        tries++;
    }
    if (tries == 0x258 || _RPCVersion != 0x115) {
        func_002AF5F0_inl1();
        DeleteSema(_RPCIRQSema);
        DeleteSema(_RPCTransferSema);
        _RPCOpen = 0;
        return 0;
    }
    _rwaGRPCDMAWorkArea = 0;
    tries = 0;
    RwaRPCTransfer(0, 0, 0, 2, 0, 0, 0, 0);
    while (!_rwaGRPCDMAWorkArea && tries < 0x258) {
        FlushCache(0);
        sceGsSyncV(0);
        tries++;
    }
    if (tries == 0x258) {
        func_002AF5F0_inl1();
        DeleteSema(_RPCIRQSema);
        DeleteSema(_RPCTransferSema);
        _RPCOpen = 0;
        return 0;
    }
    return 1;
}

// SIF command setup: buffer of 0x18 commands, handler 1 = _RwaRPCSifIntHandler (with interrupts off when threads are
// used).
// 0x002AF9A0
extern "C" void RwaSifCmdInit(void)
{
    RwInt32 was = 0;
    sceSifInitCmd();
    if (_rwaGprogramContext == 2)
        was = func_00114E60();
    sceSifSetCmdBuffer(D_01E828E0, 0x18);
    sceSifAddCmdHandler(1, _RwaRPCSifIntHandler, 0);
    if (_rwaGprogramContext == 2 && was == 1)
        EIntr();
}
