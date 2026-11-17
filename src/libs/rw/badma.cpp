// RenderWare 3.6 sky2 driver: the DMA layer (RW 3.1 file name: driver/sky2/badma.c; reading aid only).
// Written from the retail asm (decomp assembly/asm/rw/renderware.s). Tags: [C] confirmed from the asm, [L], [G].
// boot2 (track rw). Spec notes: docs/results/boot1-2026-11-05/rw/sky2_handlers.md (DMA sections).
//
// Most functions here are marked "Handwritten function" in the asm. Common shapes:
// - The packet is built in the scratchpad (0x70000000) at the cursor farFogPlane and moved to main RAM by the
//   SPR-from DMA (channel 8, D8 0x1000D000..): "flush" = D_STAT = 0x100, D8_SADR / D8_MADR / D8_QWC, D8_CHCR = 0x100.
// - Before touching D8 again the asm waits for it: if D8_CHCR & 0x100 (STR) it writes D_PCR = 0x100 (CPC8) and
//   spins on `bc0f` (COP0 condition = DMA channel 8 done). That spin cannot be expressed: it is in `#if 0` each time.
//   The STR test before it is kept.
// - Main-RAM buffer: two halves (rwPendingCtrlPtr[0..1]); the start of each half is a list of 8-byte control
//   entries {word, address} (rwDMABufTopPtr up to rwDMABufDataPtr), consumed by rwDMAHandler; data follows
//   (rwDMABufAsyncTopPtr .. rwDMABufDataTopPtr) [L, from the arithmetic below].
// - Globals with RW 3.1 names that do not fit their 3.6 role keep the table name; the role is in the comment [L].
// - The two interrupt handlers reload $gp from rwDMAPURefList and address globals with raw -0x5Exx($gp) offsets;
//   with gp = 0x004E8670 each decodes to the symbol used below (the offset is cited at each use).
// - `sync`, `sync.p`: nothing (comments). `di` / `ei` / `mfc0 Status & 0x10000`: b3hw_di / b3hw_ei / b3hw_eie.

#include "b3hw/hw.h"
#include "rw/skyglobals.h"

// [C] a qword of the DMA tags / VIF codes the asm builds with pcpyld + sq (words in memory order).
struct UnkStruct_rwDMAQword {
    unsigned int field_0;
    unsigned int field_4;
    unsigned int field_8;
    unsigned int field_C;
};

// [C] one 8-byte control entry read by rwDMAHandler: field_0 = type in bits 0-7 (1 VIF1 send, 2 GIF send, 0x21 gate
// + both, 0x40 / 0x41 flip, 0x42 PU-ref release, 0x43 nop, 0x4F callback, 0x7F jump) with the QWC in bits 12-27 and
// tag-mode bits 0x1300; field_4 = an address (the data, a counter, a callback, the next entry).
struct UnkStruct_rwDMACtrl {
    unsigned int field_0;
    unsigned int field_4;
};

// [C] an entry of the PU-reference ring (rwDMADispatchEnd[k] points at one): field_4 = the pending reference count,
// released by queueing _rwDMAAddPkt(entry, count << 12 | 0x42); rwDMAHandler's 0x42 case decrements field_0.
struct UnkStruct_rwDMAPURef {
    int field_0;
    int field_4;
};

// [C] rwDMADmaBufs 0x004E2800 (8 bytes): the half-buffer size, then the current half's index as a byte at +4.
struct UnkStruct_rwDMADmaBufs {
    unsigned int field_0;
    unsigned char field_4;
    unsigned char pad_5[3];
};

struct UnkStruct_ourGlobals {
    unsigned char pad_0[0x134];
    void *(*field_134)(unsigned int, unsigned int);   // [C] the malloc function ([L] memoryFuncs.rwmalloc)
    void (*field_138)(void *);                        // [C] the free function ([L] memoryFuncs.rwfree)
};
extern UnkStruct_ourGlobals ourGlobals;               // [C] 0x00670BD0

// --- driver globals (all gp-relative) ---
extern unsigned int rwDMAVIFGate;                 // [C] 0x004E17D4: the 0x800-byte VIF gate block (G)
extern unsigned int rwDMAHYieldFunc;              // [C] 0x004E17D8: yield callback (a guest function address or 0)
extern unsigned char rwDMAMinVsyncCnt;            // [C] 0x004E17DC: vblanks to wait between flips
extern unsigned short rwDMAPURefSize;             // [C] 0x004E17DE: entries in the PU-reference ring
extern unsigned int rwDMAbuf;                     // [C] 0x004E17E0: the DMA buffer (game-supplied or malloc'd)
extern unsigned int rwDMAbufSize;                 // [C] 0x004E17E4: its size; 0 = the driver allocated it
extern int rwVSYNCHandlerHid;                     // [C] 0x004E17E8
extern int rwDMAHandlerVIF1Hid;                   // [C] 0x004E17EC
extern int rwDMAHandlerGIFHid;                    // [C] 0x004E17F0
extern unsigned char rwDMAOwnVSYNC;               // [C] 0x004E17F4: EnableIntc(2)'s result
extern unsigned char rwDMAOwnVIF1;                // [C] 0x004E17F5: EnableDmac(1)'s result
extern unsigned char rwDMAOwnGIF;                 // [C] 0x004E17F6: EnableDmac(2)'s result
extern unsigned int _gOldRasterShowRasterFunc;    // [C] 0x004E27C4 [L] the async side's write cursor
extern unsigned int _rwDMAPktPtr;                 // [C] 0x004E27C8 [L] a game-held pointer into the packet
// [C] 0x004E27CC: four ISR bytes. [0] read by the VSYNC handler (-0x5EA4), [1] written by it (-0x5EA3), [2] a flip
// counter (-0x5EA2), [3] the flip-pending flag (-0x5EA1; set by rwDMAHandler, cleared by the VSYNC handler).
extern unsigned char _rwDMAGateSlot;              // [C] 0x004E27D0 (-0x5EA0): byte, which display buffer to show
extern unsigned int _rwDMA_local_rwDMAPktPtr;     // [C] 0x004E27DC: scratchpad packet start (0x70000000)
extern unsigned int _rwDMA_local_rwDMAAsyncPktPtr;  // [C] 0x004E27E0: main-RAM destination of the next SPR flush
extern unsigned int rwSPRBasePtr;                 // [C] 0x004E27E4
extern unsigned int rwSPRBaseInMem;               // [C] 0x004E27E8
extern unsigned int rwPendingBase;                // [C] 0x004E27EC
extern unsigned int rwPendingAsyncBase;           // [C] 0x004E27F0: where the pending control words go
// [C] 0x004E27F8 + 0x004E27FC (the asm names the second D_004E27FC): the start of each half-buffer.
extern unsigned int rwPendingCtrlPtr[];
extern UnkStruct_rwDMADmaBufs rwDMADmaBufs;       // [C] 0x004E2800
extern unsigned int rwDMADmaBufSize;              // [C] 0x004E2808 [L] the current half's data end
extern unsigned int rwDMABufDataTopPtr;           // [C] 0x004E2810 [L] the current half's end
extern unsigned int rwDMABufAsyncTopPtr;          // [C] 0x004E2814 [L] the data write position
extern unsigned int rwDMABufDataPtr;              // [C] 0x004E2818 [L] the end of the control-entry block
extern unsigned int rwDMABufTopPtr;               // [C] 0x004E281C [L] the control-entry write position
extern unsigned int rwDMABufCtrlTopPtr;           // [C] 0x004E2820
extern unsigned int rwDMABufCtrlPtr;              // [C] 0x004E2824
extern unsigned int rwDMAPktRealBase;             // [C] 0x004E2828 [L] main-RAM address of the open async header
extern unsigned int rwDMAAsyncPktRealBase;        // [C] 0x004E282C: 0 / 0x1101 open / bit 31 = async header open
extern unsigned char rwDMAPktBase;                // [C] 0x004E2830 (-0x5E40): byte, DMA transfers in flight
extern unsigned int rwDMAPktType;                 // [C] 0x004E2834 (-0x5E3C) [L] the flip data of the pending flip
extern unsigned int rwDMADispatching;             // [C] 0x004E2838 (-0x5E38): next control entry to dispatch
extern unsigned int rwDMAFlipSt;                  // [C] 0x004E283C (-0x5E34): end of the control entries to dispatch
// [C] 0x004E2840: bytes. [0] cleared by _rwDMADmaOpen; [1] (-0x5E2F) the VSYNC handler's vblank counter.
extern unsigned char rwDMADispatchPtr[];
extern unsigned int rwDMADispatchEnd;             // [C] 0x004E2844 [L] the PU-reference ring (u32 pointers)
extern unsigned short rwDMAWaited;                // [C] 0x004E2848 (lhu): ring index, first pending
extern unsigned short D_004E284A;                 // [C] 0x004E284A (lhu): ring index, end
extern unsigned int rwDMAPURefList;               // [C] 0x004E284C (-0x5E24): the $gp the ISRs reload
// [C] the four DMA-tag qwords _rwDMADmaOpen builds (the game's _rwDMAAddImageUpload refers to them by address)
extern UnkStruct_rwDMAQword rwDMAFlushAGate;      // [C] 0x00675890
extern UnkStruct_rwDMAQword rwDMAAFlush;          // [C] 0x006758A0
extern UnkStruct_rwDMAQword rwDMAFlushAGateFlush; // [C] 0x006758B0
extern UnkStruct_rwDMAQword rwDMAANull;           // [C] 0x006758C0

// [C] Sony kernel / library (eekernel.h prototypes of SDK 3.0.3; retail links 2.8.0, same calls)
extern "C" int AddIntcHandler(int cause, int (*handler)(int), int next);
extern "C" int AddDmacHandler(int channel, int (*handler)(int), int next);
extern "C" int RemoveIntcHandler(int cause, int hid);
extern "C" int RemoveDmacHandler(int channel, int hid);
extern "C" int EnableIntc(int cause);
extern "C" int EnableDmac(int channel);
extern "C" int DisableIntc(int cause);
extern "C" int DisableDmac(int channel);
extern "C" void SyncDCache(void *start, void *end);
extern "C" void InvalidDCache(void *start, void *end);
extern "C" void sceGsPutDispEnv(void *disp);

extern "C" int rwDMAHandler(int channel);
extern "C" int rwDMAVSYNCHandler(int cause);
extern "C" void _rwDMAClosePkt(void);
extern "C" int _rwDMAForceBufferSwap(void);
extern "C" void _rwDMAAddPkt(unsigned int addr, unsigned int ctrl);
extern "C" int _rwDMAHook(void);
extern "C" void _rwDMAUnhook(void);

// [C] (handwritten) the DMAC handler for channels 1 (VIF1) and 2 (GIF), also called directly with channel -1 (by
// _rwDMAForceBufferSwap, the VSYNC handler, _rwDMACallbackRestart). Registered through AddDmacHandler, so the kernel
// passes only the channel (int (*)(int), eekernel.h); the asm reads a0 only.
// Not expressed: for channel != -1 the asm saves $gp and loads it from rwDMAPURefList (0x1F17B4..0x1F17C0), and
// restores it before returning (0x1F1CA8): that only gives the raw -0x5Exx offsets their meaning. Then `sync; ei`
// (kept as b3hw_ei) and an ordinary `jr $ra` to the kernel's dispatcher: no ExitHandler-style tail. Returns -1 on
// every path [C 0x1F1CB8] ([L] for the kernel: do not call later handlers of the chain).
// Steps: on channel 1 or 2 a nonzero in-flight count rwDMAPktBase is decremented. Then, while nothing is in flight,
// entries remain (rwDMADispatching != rwDMAFlipSt) and no flip is pending (_rwDMAAsyncPktPtr[3] == 0), the entry at
// rwDMADispatching is dispatched by its type byte (see UnkStruct_rwDMACtrl) and the pointer advanced.
// 0x001F1778
extern "C" int rwDMAHandler(int channel)
{
    UnkStruct_rwDMACtrl *e;
    unsigned int w;
    unsigned int busy;
    unsigned int chcr1;
    unsigned int chcr2;
    unsigned int vif1stat;
    unsigned int vpustat;
    unsigned int gifstat;
    unsigned int next;
    int *counter;

    if ((unsigned int)(channel - 1) < 2u) {
        if (rwDMAPktBase != 0) {
            rwDMAPktBase = rwDMAPktBase - 1;
        }
    }
    while (rwDMAPktBase == 0 && rwDMADispatching != rwDMAFlipSt && _rwDMAAsyncPktPtr[3] == 0) {
        e = (UnkStruct_rwDMACtrl *)rwDMADispatching;
        w = e->field_0;
        switch (w & 0xFF) {
        case 0x21:
            // [C 0x1F1918..0x1F1A64] wait until D1 / D2 are idle, VIF1_STAT & 0x1F000003 (FQC, VPS) is 0, VPU_STAT
            // (vi29) bit 8 (VBS1) is 0 and GIF_STAT & 0xC00 (APATH) is 0; the reads per pass in the asm's order
            do {
                chcr1 = b3hw_read32(0x10009000);   // D1_CHCR
                chcr2 = b3hw_read32(0x1000A000);   // D2_CHCR
                vif1stat = b3hw_read32(0x10003C00); // VIF1_STAT
                busy = (chcr1 >> 8) & 1;
                if (chcr2 & 0x100) busy |= 2;
                if (vif1stat & 0x1F000003) busy |= 4;
                vpustat = b3hw_cop2_ctrl_read(29); // cfc2.ni vi29
                gifstat = b3hw_read32(0x10003020); // GIF_STAT
                if (vpustat & 0x100) busy |= 8;
            } while ((gifstat & 0xC00) != 0 || busy != 0);
            // [C] VIF1 FIFO <- {MSKPATH3(1) 0x06008000, 0, 0, 0} (the qword the asm keeps at sp+0x10); sync
            b3hw_write128(0x10005000, 0x06008000ULL, 0);
            while ((b3hw_read32(0x10003020) & 2) == 0) {   // GIF_STAT M3P (PATH3 masked)
            }
            b3hw_write32(0x10003010, 4);                   // GIF_MODE IMT
            b3hw_write32(0x1000A030, e[1].field_4);        // D2_TADR = word +0xC (the second entry's address)
            b3hw_write32(0x1000A020, 0);                   // D2_QWC
            b3hw_write32(0x1000A000, 0x104);               // D2_CHCR chain, STR
            b3hw_write32(0x10009030, e->field_4);          // D1_TADR
            b3hw_write32(0x10009020, 0);                   // D1_QWC
            // sync
            while ((b3hw_read32(0x10003020) & 0x1F000000) == 0) {   // GIF_STAT FQC: wait for data in the GIF FIFO
            }
            b3hw_write32(0x10009000, 0x145);               // D1_CHCR chain, TTE, STR
            rwDMADispatching = rwDMADispatching + 8;        // [C] a 16-byte entry: +8 here, +8 below
            rwDMAPktBase = rwDMAPktBase + 2;
            next = rwDMADispatching + 8;
            break;
        case 2:
            // [C 0x1F1A6C..0x1F1B48] the same wait (without the FIFO write), then GIF_MODE = 0 and D2 started
            do {
                chcr1 = b3hw_read32(0x10009000);
                chcr2 = b3hw_read32(0x1000A000);
                vif1stat = b3hw_read32(0x10003C00);
                busy = (chcr1 >> 8) & 1;
                if (chcr2 & 0x100) busy |= 2;
                if (vif1stat & 0x1F000003) busy |= 4;
                vpustat = b3hw_cop2_ctrl_read(29);
                gifstat = b3hw_read32(0x10003020);
                if (vpustat & 0x100) busy |= 8;
            } while ((gifstat & 0xC00) != 0 || busy != 0);
            b3hw_write32(0x10003010, 0);                   // GIF_MODE
            e = (UnkStruct_rwDMACtrl *)rwDMADispatching;
            if (e->field_0 & 0x300) {
                b3hw_write32(0x1000A030, e->field_4);      // D2_TADR
                b3hw_write32(0x1000A020, 0);               // D2_QWC
                b3hw_write32(0x1000A000, ((e->field_0 & 0x1300) >> 6) | 0x100);   // D2_CHCR mode / TTE bits, STR
            } else {
                b3hw_write32(0x1000A010, e->field_4);      // D2_MADR
                b3hw_write32(0x1000A020, (e->field_0 & 0x0FFFF000) >> 12);   // D2_QWC
                b3hw_write32(0x1000A000, 0x100);           // D2_CHCR normal, STR
            }
            // sync
            rwDMAPktBase = rwDMAPktBase + 1;
            next = rwDMADispatching + 8;
            break;
        case 1:
            // [C 0x1F1B50..0x1F1BA0] D1 started at once (no wait); the word tested is the one read at the loop top
            if (w & 0x300) {
                b3hw_write32(0x10009030, e->field_4);      // D1_TADR
                b3hw_write32(0x10009020, 0);               // D1_QWC
                b3hw_write32(0x10009000, ((e->field_0 & 0x1300) >> 6) | 0x101);   // D1_CHCR DIR (to VIF1), STR
            } else {
                b3hw_write32(0x10009010, e->field_4);      // D1_MADR
                b3hw_write32(0x10009020, (e->field_0 & 0x0FFFF000) >> 12);   // D1_QWC
                b3hw_write32(0x10009000, 0x101);           // D1_CHCR normal, DIR, STR
            }
            // sync
            rwDMAPktBase = rwDMAPktBase + 1;
            next = rwDMADispatching + 8;
            break;
        case 0x42:
            // [C 0x1F1BC0] release: *field_4 -= the count in bits 12-27, or 1 when it is 0
            counter = (int *)e->field_4;
            if ((w & 0x0FFFF000) != 0) {
                *counter = *counter - (int)((w & 0x0FFFF000) >> 12);
            } else {
                *counter = *counter - 1;
            }
            next = rwDMADispatching + 8;
            break;
        case 0x43:
            next = rwDMADispatching + 8;
            break;
        case 0x4F:
            // [C 0x1F1C2C] callback field_4(channel != -1) ([L] "called from the interrupt"); rwDMAPktBase = (result
            // == 0), so a 0 result holds the dispatch until _rwDMACallbackRestart. [L] the callee also finds
            // a1 = rwDMADispatching (left over); passed here as the one argument the asm sets up.
            rwDMAPktBase = (unsigned char)(((int (*)(int))e->field_4)(channel != -1) == 0);
            next = rwDMADispatching + 8;
            break;
        case 0x40:
            // [C 0x1F1BF0] flip request, display buffer 0: flip pending, rwDMAPktType = the flip data
            _rwDMAAsyncPktPtr[3] = 1;
            rwDMAPktType = e->field_4;
            _rwDMAGateSlot = 0;
            _rwDMAAsyncPktPtr[2] = _rwDMAAsyncPktPtr[2] - 1;
            next = rwDMADispatching + 8;
            break;
        case 0x41:
            // [C 0x1F1C08] the same with display buffer 1
            _rwDMAAsyncPktPtr[3] = 1;
            rwDMAPktType = e->field_4;
            _rwDMAGateSlot = 1;
            _rwDMAAsyncPktPtr[2] = _rwDMAAsyncPktPtr[2] - 1;
            next = rwDMADispatching + 8;
            break;
        case 0x7F:
            next = e->field_4;   // [C 0x1F1BB8] jump to another entry list
            break;
        default:
            next = rwDMADispatching + 8;
            break;
        }
        rwDMADispatching = next;
    }
    if (channel != -1) {
        // $gp restored; sync
        b3hw_ei();
    }
    return -1;
}

// [C] (handwritten) the INTC handler for cause 2 (VBlank start), registered through AddIntcHandler: the kernel passes
// the cause (int (*)(int)); the asm reads a0 only. Like rwDMAHandler it swaps $gp (rwDMAPURefList, 0x1F1D00..0x1F1D04)
// and on both exits restores it, `sync; ei` (kept as b3hw_ei), returns 0 with `jr $ra` [C 0x1F1D20..0x1F1D30,
// 0x1F1F24..0x1F1F30]: no ExitHandler-style tail.
// Steps: until rwDMADispatchPtr[1] reaches rwDMAMinVsyncCnt it only counts. Else, on cause 2 with a flip pending
// (_rwDMAAsyncPktPtr[3]): clear it, restart the count at 1, read GS CSR's FIELD bit, sceGsPutDispEnv of the flip
// data's environment for the field and buffer, DISPFB2 / DISPLAY2 from the flip data, optionally patch four words of
// it, then start D2 with the flip data's tag for that field / buffer, or (none) dispatch the next entries.
// [L] flip data (rwDMAPktType) = _rwDMAFlipData-shaped: DispEnvs at +0 / +0x28 / +0x350 / +0x378, the DISPFB2 /
// DISPLAY2 pairs at +0x330, +0x340, +0x3A0, +0x3B0, D2 tags at +0x3C0..+0x3CC.
// 0x001F1CE8
extern "C" int rwDMAVSYNCHandler(int cause)
{
    unsigned int flip;
    unsigned int field;
    unsigned int bit;
    unsigned int tag;
    unsigned int *w0;
    unsigned int *w1;

    if (rwDMADispatchPtr[1] < rwDMAMinVsyncCnt) {   // -0x5E2F / -0x6E94
        rwDMADispatchPtr[1] = rwDMADispatchPtr[1] + 1;
        // $gp restored; sync
        b3hw_ei();
        return 0;
    }
    if (cause == 2 && _rwDMAAsyncPktPtr[3] != 0) {  // -0x5EA1
        _rwDMAAsyncPktPtr[3] = 0;
        rwDMADispatchPtr[1] = 1;
        flip = rwDMAPktType;                        // -0x5E3C
        field = (unsigned int)(b3hw_read64(0x12001000) >> 12) & 2;   // GS CSR bit 13 (FIELD) as 0 / 2
        if (field != 0) {
            sceGsPutDispEnv((void *)(flip + 0x350 + (_rwDMAGateSlot & 1) * 0x28));   // -0x5EA0
        } else {
            sceGsPutDispEnv((void *)(flip + (_rwDMAGateSlot & 1) * 0x28));
        }
        if (_rwDMAGateSlot != 0) {
            if (field != 0) {
                b3hw_write64(0x12000070, *(unsigned long long *)(flip + 0x3B0));   // DISPFB2
                b3hw_write64(0x12000080, *(unsigned long long *)(flip + 0x3B8));   // DISPLAY2
            } else {
                b3hw_write64(0x12000070, *(unsigned long long *)(flip + 0x340));
                b3hw_write64(0x12000080, *(unsigned long long *)(flip + 0x348));
            }
        } else {
            if (field != 0) {
                b3hw_write64(0x12000070, *(unsigned long long *)(flip + 0x3A0));
                b3hw_write64(0x12000080, *(unsigned long long *)(flip + 0x3A8));
            } else {
                b3hw_write64(0x12000070, *(unsigned long long *)(flip + 0x330));
                b3hw_write64(0x12000080, *(unsigned long long *)(flip + 0x338));
            }
        }
        if (_rwDMAAsyncPktPtr[0] != 0) {            // -0x5EA4
            if (field != 0) {
                _rwDMAAsyncPktPtr[1] = 0;           // -0x5EA3
            } else {
                _rwDMAAsyncPktPtr[1] = 1;
            }
            // [C 0x1F1E5C..0x1F1EBC] bit 3 of two words of the flip data = 8 when field is 0, else 0
            bit = (field ^ 2) << 2;
            if (_rwDMAGateSlot != 0) {
                w0 = (unsigned int *)(flip + 0x1F4);
                w1 = (unsigned int *)(flip + 0x274);
            } else {
                w0 = (unsigned int *)(flip + 0x84);
                w1 = (unsigned int *)(flip + 0x104);
            }
            *w0 = (*w0 & 0xFFFFFFF7) | bit;
            *w1 = (*w1 & 0xFFFFFFF7) | bit;
        }
        tag = *(unsigned int *)(flip + 0x3C0 + ((field | (_rwDMAGateSlot & 1)) << 2));
        if (tag != 0) {
            b3hw_write32(0x1000A030, tag);          // D2_TADR
            b3hw_write32(0x1000A020, 0);            // D2_QWC
            b3hw_write32(0x1000A000, 0x104);        // D2_CHCR chain, STR
            // sync
            rwDMAPktBase = rwDMAPktBase + 1;        // -0x5E40
        } else {
            rwDMAHandler(-1);
        }
    }
    // $gp restored; sync
    b3hw_ei();
    return 0;
}

// [C] (handwritten) closes the open packet: an open async header (rwDMAAsyncPktRealBase bit 31) gets its DMAtag/VIF
// DIRECT qword (or is dropped when empty); the packet gets its tail qwords (0x70000000 = DMAtag end, and with an
// async cursor the four-qword ending); the scratchpad part is flushed to main RAM; the pending control words are
// written and the new pending state taken from the buffer pointers; then the PU-reference ring is released.
// With no packet open (rwDMAAsyncPktRealBase == 0) only the ring is released. No defined result.
// 0x001F1F50
extern "C" void _rwDMAClosePkt(void)
{
    unsigned int n;
    unsigned int len;
    unsigned int k;
    unsigned int end;
    unsigned int spr;
    unsigned int dst;
    unsigned int p;
    unsigned int top;
    unsigned int ctrl;
    unsigned int *pend;
    int count;
    UnkStruct_rwDMAQword *q;
    UnkStruct_rwDMAPURef *ref;

    if (rwDMAAsyncPktRealBase == 0) {
        if (rwDMAWaited != D_004E284A) {
            k = rwDMAWaited;
            end = D_004E284A;
            rwDMAWaited = 0;
            D_004E284A = 0;
            do {
                ref = *(UnkStruct_rwDMAPURef **)(rwDMADispatchEnd + k * 4);
                count = ref->field_4;
                ref->field_4 = 0;
                while (count > 0) {
                    _rwDMAAddPkt(*(unsigned int *)(rwDMADispatchEnd + k * 4),
                                 ((unsigned int)(count > 0xFFFF ? 0xFFFF : count) << 12) | 0x42);
                    count -= 0xFFFF;
                }
                k = (k + 1) & 0xFFFF;
                if (k == rwDMAPURefSize) k = 0;
            } while (k != end);
        }
        return;
    }
    if ((int)rwDMAAsyncPktRealBase < 0) {
        // [C 0x1F201C] close the async header at rwDMAPktRealBase: QWC = bytes written since it, less the header
        n = (((_rwDMA_local_rwDMAAsyncPktPtr - rwDMAPktRealBase) + (farFogPlane - _rwDMA_local_rwDMAPktPtr)) >> 4) - 1;
        if (n != 0) {
            // [C] the asm stores through the uncached alias rwDMAPktRealBase | 0x20000000; it is main RAM
            // (rwDMAPktRealBase is set from _rwDMA_local_rwDMAAsyncPktPtr), so a plain store with the bit cleared
            q = (UnkStruct_rwDMAQword *)(rwDMAPktRealBase & ~0x20000000u);
            q->field_0 = n | 0x10000000;    // DMAtag cnt, QWC n
            q->field_4 = 0;
            q->field_8 = 0;
            q->field_C = n | 0x50000000;    // VIF DIRECT n
        } else {
            _rwDMA_local_rwDMAAsyncPktPtr = _rwDMA_local_rwDMAAsyncPktPtr - 0x10;
        }
        rwDMAPktRealBase = 0;
        rwDMAAsyncPktRealBase = rwDMAAsyncPktRealBase & 0x7FFFFFFF;
    }
    if (_gOldRasterShowRasterFunc != 0) {
        if (rwDMAAsyncPktRealBase != 0x102) {
            // [C 0x1F20B0..0x1F2138] the async cursor gets DMAtag end; the packet four qwords
            q = (UnkStruct_rwDMAQword *)_gOldRasterShowRasterFunc;
            q->field_0 = 0x70000000;
            q->field_4 = 0;
            q->field_8 = 0;
            q->field_C = 0;
            q = (UnkStruct_rwDMAQword *)farFogPlane;
            q[0].field_0 = 0x70000003;      // DMAtag end, QWC 3
            q[0].field_4 = 0;
            q[0].field_8 = 0x11000000;      // VIF FLUSH
            q[0].field_C = 0x50000002;      // VIF DIRECT 2
            farFogPlane = farFogPlane + 0x10;
            q[1].field_0 = 0x8001;          // GIFtag NLOOP 1, EOP; A+D, 1 register
            q[1].field_4 = 0x10000000;
            q[1].field_8 = 0xE;
            q[1].field_C = 0;
            farFogPlane = farFogPlane + 0x10;
            q[2].field_0 = 0;
            q[2].field_4 = 0;
            q[2].field_8 = 0x7F;            // [L] A+D register 0x7F (a no-op write)
            q[2].field_C = 0;
            farFogPlane = farFogPlane + 0x10;
            q[3].field_0 = 0x13000000;      // VIF FLUSHA
            q[3].field_4 = 0;
            q[3].field_8 = 0x06000000;      // VIF MSKPATH3(0)
            q[3].field_C = 0;
            farFogPlane = farFogPlane + 0x10;
        } else {
            q = (UnkStruct_rwDMAQword *)farFogPlane;
            q->field_0 = 0x70000000;
            q->field_4 = 0;
            q->field_8 = 0;
            q->field_C = 0;
            farFogPlane = farFogPlane + 0x10;
        }
    } else {
        q = (UnkStruct_rwDMAQword *)farFogPlane;
        q->field_0 = 0x70000000;            // DMAtag end
        q->field_4 = 0;
        q->field_8 = 0x06000000;            // VIF MSKPATH3(0)
        q->field_C = 0;
        farFogPlane = farFogPlane + 0x10;
    }
    _rwDMAPktPtr = 0;
    len = 0;
    if (farFogPlane != 0) {
        len = farFogPlane - _rwDMA_local_rwDMAPktPtr;
    }
    if (len != 0) {
        if (b3hw_read32(0x1000D000) & 0x100) {   // D8_CHCR STR
            b3hw_write32(0x1000E020, 0x100);     // D_PCR CPC8
            // sync; sync.p
#if 0
            // [C 0x1F21B8..0x1F21D0] `bc0f` loop: spin until the COP0 condition (DMA channel 8 done) is true
#endif
        }
        spr = _rwDMA_local_rwDMAPktPtr;
        b3hw_write32(0x1000E010, 0x100);         // D_STAT: clear CIS8
        b3hw_write32(0x1000D080, spr);           // D8_SADR
        dst = _rwDMA_local_rwDMAAsyncPktPtr;
        b3hw_write32(0x1000D010, dst);           // D8_MADR
        b3hw_write32(0x1000D020, len >> 4);      // D8_QWC
        b3hw_write32(0x1000D000, 0x100);         // D8_CHCR STR (from SPR)
        // sync; sync.p
        p = _rwDMAPktPtr;
        if (p != 0 && (p & 0xFFFFC000) == 0x70000000) {
            // [C] a scratchpad pointer is moved to the uncached alias of where its bytes now go (a value only)
            _rwDMAPktPtr = (dst + (p - spr)) | 0x20000000;
        }
        _rwDMA_local_rwDMAAsyncPktPtr = _rwDMA_local_rwDMAAsyncPktPtr + len;
        farFogPlane = _rwDMA_local_rwDMAPktPtr;
        if (b3hw_read32(0x1000D000) & 0x100) {
            b3hw_write32(0x1000E020, 0x100);
            // sync; sync.p
#if 0
            // [C 0x1F22A0..0x1F22B8] `bc0f` loop: spin until DMA channel 8 is done
#endif
        }
    }
    if (rwPendingBase != 0) {
        pend = (unsigned int *)rwPendingAsyncBase;
        if (rwSPRBaseInMem != 0) {
            pend[2] = 0x122;
            pend[1] = rwSPRBasePtr;
            pend[3] = rwSPRBaseInMem;
            pend[0] = 0x1121;
            rwPendingAsyncBase = rwPendingAsyncBase + 8;
        } else {
            pend[0] = rwPendingBase;
            pend[1] = rwSPRBasePtr;
        }
    }
    top = rwDMABufTopPtr;
    ctrl = rwDMABufCtrlPtr;
    rwPendingAsyncBase = top;
    rwSPRBasePtr = rwDMABufCtrlTopPtr;
    rwPendingBase = rwDMAAsyncPktRealBase;
    rwSPRBaseInMem = ctrl;
    rwDMABufTopPtr = top + 8;
    if (ctrl != 0) {
        rwDMABufTopPtr = top + 0x10;
    }
    _gOldRasterShowRasterFunc = 0;
    farFogPlane = 0;
    rwDMABufCtrlTopPtr = 0;
    rwDMABufCtrlPtr = 0;
    rwDMAAsyncPktRealBase = 0;
    if (rwDMAWaited != D_004E284A) {
        k = rwDMAWaited;
        end = D_004E284A;
        rwDMAWaited = 0;
        D_004E284A = 0;
        do {
            ref = *(UnkStruct_rwDMAPURef **)(rwDMADispatchEnd + k * 4);
            count = ref->field_4;
            ref->field_4 = 0;
            while (count > 0) {
                _rwDMAAddPkt(*(unsigned int *)(rwDMADispatchEnd + k * 4),
                             ((unsigned int)(count > 0xFFFF ? 0xFFFF : count) << 12) | 0x42);
                count -= 0xFFFF;
            }
            k = (k + 1) & 0xFFFF;
            if (k == rwDMAPURefSize) k = 0;
        } while (k != end);
    }
}

// [C] (handwritten) closes the packet, writes the pending control words, lets the yield callback run until the
// dispatcher has caught up (rwDMADispatching == rwDMAFlipSt and nothing in flight), then with interrupts off hands
// the finished half to the dispatcher (rwDMAFlipSt = rwDMABufTopPtr, rwDMADispatching = the half's start), switches
// to the other half (pointers reset, cache invalidated) and runs rwDMAHandler(-1) with interrupts off.
// Returns rwDMAHandler's result (-1) [C: v0 is left from the jal at 0x1F25FC; ei does not change it].
// 0x001F2408
extern "C" int _rwDMAForceBufferSwap(void)
{
    unsigned int *pend;
    unsigned int room;
    unsigned int half;
    unsigned int base;
    unsigned int size;
    int waiting;
    int r;

    _rwDMAClosePkt();
    if (rwPendingBase != 0) {
        if (b3hw_read32(0x1000D000) & 0x100) {   // D8_CHCR STR
            b3hw_write32(0x1000E020, 0x100);     // D_PCR CPC8
            // sync; sync.p
#if 0
            // [C 0x1F2458..0x1F2470] `bc0f` loop: spin until DMA channel 8 is done
#endif
        }
        pend = (unsigned int *)rwPendingAsyncBase;
        if (rwSPRBaseInMem != 0) {
            pend[2] = 0x122;
            pend[1] = rwSPRBasePtr;
            pend[3] = rwSPRBaseInMem;
            pend[0] = 0x1121;
            rwPendingAsyncBase = rwPendingAsyncBase + 8;
        } else {
            pend[0] = rwPendingBase;
            pend[1] = rwSPRBasePtr;
        }
        rwSPRBasePtr = 0;
        rwSPRBaseInMem = 0;
        rwPendingBase = 0;
        rwPendingAsyncBase = 0;
    }
    room = rwDMABufDataTopPtr - rwDMABufAsyncTopPtr;
    do {
        waiting = 0;
        if (rwDMADispatching != rwDMAFlipSt) {
            waiting = 1;
        } else if (rwDMAPktBase != 0) {
            waiting = 1;
        }
        if (rwDMAHYieldFunc != 0) {
            ((void (*)(int, unsigned int))rwDMAHYieldFunc)(waiting, room);   // [L] (busy, bytes still free)
        }
        // rt1: not in retail -- the wait found the dispatcher behind; an interactive host jumps virtual time to its next
        // event here (b3hw_idle_wait; a no-op in proof runs). The game's yield callback (func_00228040) only keeps the
        // lowest free room seen, the same for every pass of this loop
        if (waiting != 0) b3hw_idle_wait();
    } while (waiting != 0);
    do {
        b3hw_di();
        // sync.p
    } while (b3hw_eie());
    rwDMAFlipSt = rwDMABufTopPtr;
    rwDMADispatching = rwPendingCtrlPtr[rwDMADmaBufs.field_4];
    b3hw_ei();
    rwDMADmaBufs.field_4 = rwDMADmaBufs.field_4 ^ 1;
    half = rwDMADmaBufs.field_4;
    base = rwPendingCtrlPtr[half];
    size = rwDMADmaBufs.field_0;
    rwDMABufAsyncTopPtr = rwPendingCtrlPtr[half] + 0x80;
    rwDMABufDataPtr = rwPendingCtrlPtr[half] + 0x78;
    rwDMADmaBufSize = base + size;
    rwDMABufTopPtr = base;
    rwDMABufDataTopPtr = base + size;
    farFogPlane = 0;
    _gOldRasterShowRasterFunc = 0;
    InvalidDCache((void *)rwPendingCtrlPtr[rwDMADmaBufs.field_4],
                  (void *)(rwPendingCtrlPtr[rwDMADmaBufs.field_4] + size - 1));
    do {
        b3hw_di();
        // sync.p
    } while (b3hw_eie());
    r = rwDMAHandler(-1);
    b3hw_ei();
    return r;
}

// [C] (handwritten) appends the control entry {ctrl, addr} at rwDMABufTopPtr (closing an open packet and writing the
// pending control words first). When the entry block is full a 0x7F jump entry to a fresh 64-aligned block at the
// data position is written instead and the entry goes there; when even that does not fit, the entry is written
// anyway and the buffers are swapped. No defined result.
// 0x001F2620
extern "C" void _rwDMAAddPkt(unsigned int addr, unsigned int ctrl)
{
    unsigned int *pend;
    unsigned int *top;
    unsigned int blk;

    if ((farFogPlane | _gOldRasterShowRasterFunc) != 0) {
        _rwDMAClosePkt();
    }
    if (rwPendingBase != 0) {
        if (b3hw_read32(0x1000D000) & 0x100) {   // D8_CHCR STR
            b3hw_write32(0x1000E020, 0x100);     // D_PCR CPC8
            // sync; sync.p
#if 0
            // [C 0x1F2684..0x1F269C] `bc0f` loop: spin until DMA channel 8 is done
#endif
        }
        pend = (unsigned int *)rwPendingAsyncBase;
        if (rwSPRBaseInMem != 0) {
            pend[2] = 0x122;
            pend[1] = rwSPRBasePtr;
            pend[3] = rwSPRBaseInMem;
            pend[0] = 0x1121;
            rwPendingAsyncBase = rwPendingAsyncBase + 8;
        } else {
            pend[0] = rwPendingBase;
            pend[1] = rwSPRBasePtr;
        }
        rwSPRBasePtr = 0;
        rwSPRBaseInMem = 0;
        rwPendingBase = 0;
        rwPendingAsyncBase = 0;
    }
    top = (unsigned int *)rwDMABufTopPtr;
    if (rwDMABufTopPtr < rwDMABufDataPtr) {
        top[0] = ctrl;
        top[1] = addr;
        rwDMABufTopPtr = rwDMABufTopPtr + 8;
        return;
    }
    blk = (rwDMABufAsyncTopPtr + 0x3F) & 0xFFFFFFC0;
    if (rwDMABufDataTopPtr < blk + 0x80) {
        top[0] = ctrl;
        top[1] = addr;
        rwDMABufTopPtr = rwDMABufTopPtr + 8;
        _rwDMAForceBufferSwap();
        return;
    }
    top[1] = blk;
    top[0] = 0x7F;
    rwDMABufAsyncTopPtr = blk + 0x80;
    rwDMABufDataPtr = blk + 0x78;
    ((unsigned int *)blk)[0] = ctrl;
    ((unsigned int *)blk)[1] = addr;
    rwDMABufTopPtr = blk + 8;
}

// [C] (handwritten) reserves room for a packet of nQW qwords (plus 4 header/tail qwords, plus 1 for an async
// packet: type bit 31) and leaves farFogPlane at the write slot. type != 0 also opens an async header (a DMAtag /
// VIF DIRECT qword at rwDMAPktRealBase, filled in when the packet closes). An open packet of the same kind is
// extended when it still fits (flushing the scratchpad when more than 0x2000 bytes would be in it); else it is
// closed and a new one opened in the current half; when the half is full the buffers are swapped and it retries.
// Returns 1, or 0 when even an empty half (rwDMADmaBufs.field_0 bytes) is too small for it.
// [C] each retry adds 4 to the qword count again (0x1F30A0 is inside the loop); kept.
// 0x001F3080
extern "C" int _rwDMAOpenVIFPkt(unsigned int type, unsigned int nQW)
{
    unsigned int n;
    unsigned int real;
    unsigned int bytes;
    unsigned int used;
    unsigned int len;
    unsigned int dst;
    unsigned int spr;
    unsigned int p;
    unsigned int cur;
    unsigned int pkt;
    unsigned int st;
    unsigned int m;
    unsigned int pr;
    unsigned int top;
    unsigned int avail;
    unsigned int blk;
    unsigned int base;
    unsigned int end;
    UnkStruct_rwDMAQword *q;

    n = nQW;
    real = rwDMAAsyncPktRealBase;
    for (;;) {
        n = n + 4;
        if ((type & 0x80000000) & ~real) {
            n = n + 1;
        }
        if (real == 0) goto fresh;
        if ((real & 0x7FFFFFFF) != 0x1101) goto close;
        bytes = n << 4;
        used = farFogPlane - _rwDMA_local_rwDMAPktPtr;
        if (!(_rwDMA_local_rwDMAAsyncPktPtr + used + bytes < rwDMABufDataTopPtr)) goto close;
        if (used + bytes > 0x2000) {
            // [C 0x1F3110] flush the scratchpad first (the same sequence as in _rwDMAClosePkt)
            len = 0;
            if (farFogPlane != 0) {
                len = used;
            }
            if (len != 0) {
                if (b3hw_read32(0x1000D000) & 0x100) {   // D8_CHCR STR
                    b3hw_write32(0x1000E020, 0x100);     // D_PCR CPC8
                    // sync; sync.p
#if 0
                    // [C 0x1F314C..0x1F3164] `bc0f` loop: spin until DMA channel 8 is done
#endif
                }
                spr = _rwDMA_local_rwDMAPktPtr;
                b3hw_write32(0x1000E010, 0x100);         // D_STAT
                b3hw_write32(0x1000D080, spr);           // D8_SADR
                dst = _rwDMA_local_rwDMAAsyncPktPtr;
                b3hw_write32(0x1000D010, dst);           // D8_MADR
                b3hw_write32(0x1000D020, len >> 4);      // D8_QWC
                b3hw_write32(0x1000D000, 0x100);         // D8_CHCR
                // sync; sync.p
                p = _rwDMAPktPtr;
                if (p != 0 && (p & 0xFFFFC000) == 0x70000000) {
                    _rwDMAPktPtr = (dst + (p - spr)) | 0x20000000;
                }
                _rwDMA_local_rwDMAAsyncPktPtr = _rwDMA_local_rwDMAAsyncPktPtr + len;
                farFogPlane = _rwDMA_local_rwDMAPktPtr;
                if (b3hw_read32(0x1000D000) & 0x100) {
                    b3hw_write32(0x1000E020, 0x100);
                    // sync; sync.p
#if 0
                    // [C 0x1F3230..0x1F3248] `bc0f` loop: spin until DMA channel 8 is done
#endif
                }
            }
        }
        // [C 0x1F3254]
        dst = _rwDMA_local_rwDMAAsyncPktPtr;
        cur = farFogPlane;
        pkt = _rwDMA_local_rwDMAPktPtr;
        st = rwDMAAsyncPktRealBase;
        rwDMABufAsyncTopPtr = dst + bytes + cur - pkt;
        if ((st & 0x80000000) == type) {   // [C] the whole type is compared with the state's bit 31
            return 1;
        }
        if (st & (~type & 0x80000000)) {
            // [C 0x1F3294] an async header is open but this packet is not async: close the header
            pr = rwDMAPktRealBase;
            m = (((dst - pr) + (cur - pkt)) >> 4) - 1;
            if (m != 0) {
                // [C] uncached-alias store (pr | 0x20000000) to main RAM: a plain store with the bit cleared
                q = (UnkStruct_rwDMAQword *)(pr & ~0x20000000u);
                q->field_0 = m | 0x10000000;    // DMAtag cnt
                q->field_4 = 0;
                q->field_8 = 0;
                q->field_C = m | 0x50000000;    // VIF DIRECT
            } else {
                _rwDMA_local_rwDMAAsyncPktPtr = dst - 0x10;
            }
            rwDMAPktRealBase = 0;
            rwDMAAsyncPktRealBase = rwDMAAsyncPktRealBase & 0x7FFFFFFF;
        }
        if ((~rwDMAAsyncPktRealBase & (type & 0x80000000)) == 0) {
            return 1;
        }
        // [C 0x1F331C] async packet, no header open: flush the scratchpad, then open a header qword in main RAM
        len = 0;
        if (farFogPlane != 0) {
            len = farFogPlane - _rwDMA_local_rwDMAPktPtr;
        }
        if (len != 0) {
            if (b3hw_read32(0x1000D000) & 0x100) {
                b3hw_write32(0x1000E020, 0x100);
                // sync; sync.p
#if 0
                // [C 0x1F3360..0x1F3378] `bc0f` loop: spin until DMA channel 8 is done
#endif
            }
            spr = _rwDMA_local_rwDMAPktPtr;
            b3hw_write32(0x1000E010, 0x100);
            b3hw_write32(0x1000D080, spr);
            dst = _rwDMA_local_rwDMAAsyncPktPtr;
            b3hw_write32(0x1000D010, dst);
            b3hw_write32(0x1000D020, len >> 4);
            b3hw_write32(0x1000D000, 0x100);
            // sync; sync.p
            p = _rwDMAPktPtr;
            if (p != 0 && (p & 0xFFFFC000) == 0x70000000) {
                _rwDMAPktPtr = (dst + (p - spr)) | 0x20000000;
            }
            _rwDMA_local_rwDMAAsyncPktPtr = _rwDMA_local_rwDMAAsyncPktPtr + len;
            farFogPlane = _rwDMA_local_rwDMAPktPtr;
            if (b3hw_read32(0x1000D000) & 0x100) {
                b3hw_write32(0x1000E020, 0x100);
                // sync; sync.p
#if 0
                // [C 0x1F3448..0x1F3460] `bc0f` loop: spin until DMA channel 8 is done
#endif
            }
        }
        dst = _rwDMA_local_rwDMAAsyncPktPtr;
        rwDMAPktRealBase = dst;
        rwDMAAsyncPktRealBase = rwDMAAsyncPktRealBase | 0x80000000;
        _rwDMA_local_rwDMAAsyncPktPtr = dst + 0x10;
        return 1;

    close:
        // [C 0x1F348C] the async extra qword counted above is taken back (_rwDMAClosePkt opens nothing)
        if ((type & 0x80000000) & ~real) {
            n = n - 1;
        }
        _rwDMAClosePkt();
    fresh:
        // [C 0x1F34A8] a control slot is needed (8 bytes before the end of the entry block); when there is none, a
        // 0x7F jump entry to a new 64-aligned entry block at the data position, if 0x80 bytes fit
        top = rwDMABufTopPtr;
        avail = rwDMABufAsyncTopPtr;
        if (!(top < rwDMABufDataPtr - 8)) {
            blk = (rwDMABufAsyncTopPtr + 0x3F) & 0xFFFFFFC0;
            if (blk + 0x80 < rwDMABufDataTopPtr) {
                ((unsigned int *)top)[1] = blk;
                rwDMABufTopPtr = blk;
                rwDMABufDataPtr = blk + 0x78;
                ((unsigned int *)top)[0] = 0x7F;
                avail = blk + 0x80;
                rwDMABufAsyncTopPtr = blk + 0x80;
            } else {
                avail = 0;
            }
        }
        if (type != 0) {
            n = n + 1;
        }
        if (avail != 0) {
            base = (avail + 0x7F) & 0xFFFFFF80;
            end = base + (n << 4);
            if (!(rwDMABufDataTopPtr < end)) {
                rwDMABufAsyncTopPtr = end;
                farFogPlane = _rwDMA_local_rwDMAPktPtr;
                rwDMAAsyncPktRealBase = 0x1101;
                rwDMABufCtrlTopPtr = base;
                _rwDMA_local_rwDMAAsyncPktPtr = base;
                if (type != 0) {
                    _rwDMA_local_rwDMAAsyncPktPtr = base + 0x10;
                    rwDMAAsyncPktRealBase = 0x80001101;
                    rwDMAPktRealBase = base;
                }
                return 1;
            }
        }
        if (type != 0) {
            n = n - 1;
        }
        if (rwDMADmaBufs.field_0 < (n << 4) + 0xC0) {
            return 0;
        }
        _rwDMAForceBufferSwap();
        real = rwDMAAsyncPktRealBase;
    }
}

// [C] installs the handlers: rwDMAPURefList = $gp (for the ISRs; [C 0x1F3C1C] `sw $gp`: the host stores the guest
// gp value 0x004E8670 [C: the image's gp, checked by %gp_rel(rwDMAVIFGate) = 0x9164; the host has no $gp]), then
// AddIntcHandler(2 VBlank start, rwDMAVSYNCHandler), AddDmacHandler(1 VIF1, rwDMAHandler), AddDmacHandler(2 GIF,
// rwDMAHandler), undoing the earlier ones when one fails (-1). Then rwDMADispatchPtr[1] = 1, the Enable results in
// the Own bytes, VIF0_ERR |= 2 (ME0) and VIF1_ERR = that value, D_CTRL &= 0xFFFFF8FD (RELE, RCYC off).
// Returns 1, or 0 on a failure.
// 0x001F3C08
extern "C" int _rwDMAHook(void)
{
    int hid;
    unsigned int err;

    rwDMAPURefList = 0x004E8670;
    rwVSYNCHandlerHid = AddIntcHandler(2, rwDMAVSYNCHandler, 0);
    if (rwVSYNCHandlerHid == -1) {
        return 0;
    }
    rwDMAHandlerVIF1Hid = AddDmacHandler(1, rwDMAHandler, 0);
    if (rwDMAHandlerVIF1Hid == -1) {
        RemoveIntcHandler(2, rwVSYNCHandlerHid);
        rwVSYNCHandlerHid = -1;
        return 0;
    }
    hid = AddDmacHandler(2, rwDMAHandler, 0);
    rwDMAHandlerGIFHid = hid;
    if (hid == -1) {
        RemoveDmacHandler(1, rwDMAHandlerVIF1Hid);
        rwDMAHandlerVIF1Hid = hid;
        RemoveIntcHandler(2, rwVSYNCHandlerHid);
        rwVSYNCHandlerHid = hid;
        return 0;
    }
    rwDMADispatchPtr[1] = 1;
    rwDMAOwnVSYNC = (unsigned char)EnableIntc(2);
    rwDMAOwnVIF1 = (unsigned char)EnableDmac(1);
    rwDMAOwnGIF = (unsigned char)EnableDmac(2);
    err = b3hw_read32(0x10003820) | 2;              // VIF0_ERR
    b3hw_write32(0x10003820, err);
    b3hw_write32(0x10003C20, err);                  // VIF1_ERR [C 0x1F3CE8: the same register value]
    b3hw_write32(0x1000E000, b3hw_read32(0x1000E000) & 0xFFFFF8FD);   // D_CTRL
    return 1;
}

// [C] (handwritten) closes the packet, flushes pending control words, releases the PU-reference ring, swaps the
// buffers when the current half holds entries, waits until the ISRs are idle (rwDMAPktBase and the flip-pending
// byte 0), disables what _rwDMAHook enabled and removes the three handlers (ids = -1). No defined result.
// 0x001F3D38
extern "C" void _rwDMAUnhook(void)
{
    unsigned int *pend;
    unsigned int k;
    unsigned int end;
    int count;
    UnkStruct_rwDMAPURef *ref;

    _rwDMAClosePkt();
    if (rwPendingBase != 0) {
        if (b3hw_read32(0x1000D000) & 0x100) {   // D8_CHCR STR
            b3hw_write32(0x1000E020, 0x100);     // D_PCR CPC8
            // sync; sync.p
#if 0
            // [C 0x1F3D94..0x1F3DAC] `bc0f` loop: spin until DMA channel 8 is done
#endif
        }
        pend = (unsigned int *)rwPendingAsyncBase;
        if (rwSPRBaseInMem != 0) {
            pend[2] = 0x122;
            pend[1] = rwSPRBasePtr;
            pend[3] = rwSPRBaseInMem;
            pend[0] = 0x1121;
            rwPendingAsyncBase = rwPendingAsyncBase + 8;
        } else {
            pend[0] = rwPendingBase;
            pend[1] = rwSPRBasePtr;
        }
        rwSPRBasePtr = 0;
        rwSPRBaseInMem = 0;
        rwPendingBase = 0;
        rwPendingAsyncBase = 0;
    }
    if (rwDMAWaited != D_004E284A) {
        k = rwDMAWaited;
        end = D_004E284A;
        rwDMAWaited = 0;
        D_004E284A = 0;
        do {
            ref = *(UnkStruct_rwDMAPURef **)(rwDMADispatchEnd + k * 4);
            count = ref->field_4;
            ref->field_4 = 0;
            while (count > 0) {
                _rwDMAAddPkt(*(unsigned int *)(rwDMADispatchEnd + k * 4),
                             ((unsigned int)(count > 0xFFFF ? 0xFFFF : count) << 12) | 0x42);
                count -= 0xFFFF;
            }
            k = (k + 1) & 0xFFFF;
            if (k == rwDMAPURefSize) k = 0;
        } while (k != end);
    }
    if (rwPendingCtrlPtr[rwDMADmaBufs.field_4] != rwDMABufTopPtr) {
        _rwDMAForceBufferSwap();
    }
    // [C 0x1F3EC8..0x1F3EF4] busy-wait on the ISR bytes (only the handlers clear them)
    for (;;) {
        if (rwDMAPktBase != 0) continue;
        if (_rwDMAAsyncPktPtr[3] != 0) continue;
        if (rwDMAPktBase != 0) continue;
        break;
    }
    if (rwDMAOwnGIF != 0) {
        DisableDmac(2);
        rwDMAOwnGIF = 0;
    }
    if (rwDMAOwnVIF1 != 0) {
        DisableDmac(1);
        rwDMAOwnVIF1 = 0;
    }
    if (rwDMAOwnVSYNC != 0) {
        DisableIntc(2);
        rwDMAOwnVSYNC = 0;
    }
    RemoveDmacHandler(2, rwDMAHandlerGIFHid);
    rwDMAHandlerGIFHid = -1;
    RemoveDmacHandler(1, rwDMAHandlerVIF1Hid);
    rwDMAHandlerVIF1Hid = -1;
    RemoveIntcHandler(2, rwVSYNCHandlerHid);
    rwVSYNCHandlerHid = -1;
}

// [C] opens the DMA layer: S = rwDMAbufSize or 0x100000; allocates S + 0x80 bytes (hint 0x40411) when no buffer was
// supplied (B3 supplies one through _rwDMAPreAlloc); installs the handlers (_rwDMAHook; on failure frees a buffer it
// allocated and returns 0, rwDMAbuf kept); lays out G = rwDMAbuf rounded up to 0x80 (the 0x800-byte VIF gate), the
// two halves of H = ((S - 0x800 - rwDMAPURefSize * 4) >> 1) & ~0x7F bytes from B = G + 0x800, the PU-reference ring at
// B + 2H; builds the gate and the four tag qwords; SyncDCache(G, G + 2H + 0x7FF); resets the ISR state. Returns 1.
// 0x001F3F98
extern "C" int _rwDMADmaOpen(void)
{
    unsigned int size;
    unsigned int gate;
    unsigned int half;
    unsigned int buf;
    int i;
    UnkStruct_rwDMAQword *q;

    size = 0x100000;
    if (rwDMAbufSize != 0) {
        size = rwDMAbufSize;
    }
    if (rwDMAbuf == 0) {
        rwDMAbuf = (unsigned int)ourGlobals.field_134(size + 0x80, 0x40411);
        if (rwDMAbuf == 0) {
            return 0;
        }
        rwDMAbufSize = 0;
    }
    gate = (rwDMAbuf + 0x7F) & 0xFFFFFF80;
    if (_rwDMAHook() == 0) {
        if (rwDMAbufSize != 0) {
            return 0;
        }
        ourGlobals.field_138((void *)rwDMAbuf);
        return 0;
    }
    rwDMAVIFGate = gate;
    buf = gate + 0x800;
    size = (size - 0x800) - (unsigned int)rwDMAPURefSize * 4;
    rwPendingCtrlPtr[0] = buf;
    half = (size >> 1) & 0xFFFFFF80;
    rwDMADmaBufs.field_4 = 0;
    rwDMAWaited = 0;
    rwDMADmaBufs.field_0 = half;
    rwPendingCtrlPtr[1] = buf + half;          // [C] D_004E27FC
    rwDMADispatchEnd = buf + half + half;
    D_004E284A = 0;
    rwDMABufTopPtr = buf;
    farFogPlane = 0;
    rwDMABufDataPtr = buf + 0x78;
    rwDMABufAsyncTopPtr = buf + 0x80;
    rwDMADmaBufSize = buf + half;
    rwDMABufDataTopPtr = buf + half;
    _gOldRasterShowRasterFunc = 0;
    rwDMABufCtrlTopPtr = 0;
    rwDMAPktRealBase = 0;
    rwDMAAsyncPktRealBase = 0;
    // [C 0x1F40B8..0x1F4100] the gate's 0x800 bytes cleared, from the top down
    q = (UnkStruct_rwDMAQword *)rwDMAVIFGate;
    for (i = 0x7F; i >= 0; i--) {
        q[i].field_0 = 0;
        q[i].field_4 = 0;
        q[i].field_8 = 0;
        q[i].field_C = 0;
    }
    q = (UnkStruct_rwDMAQword *)rwDMAVIFGate;
    q[0x07].field_0 = 0x11000000;              // +0x070 VIF FLUSH, 0, 0, MSKPATH3(0)
    q[0x07].field_4 = 0;
    q[0x07].field_8 = 0;
    q[0x07].field_C = 0x06000000;
    q[0x20].field_0 = 0x06008000;              // +0x200 MSKPATH3(1)
    q[0x20].field_4 = 0;
    q[0x20].field_8 = 0;
    q[0x20].field_C = 0;
    q[0x21].field_0 = 0x11000000;              // +0x210 FLUSH, FLUSHA, FLUSH, FLUSHA
    q[0x21].field_4 = 0x13000000;
    q[0x21].field_8 = 0x11000000;
    q[0x21].field_C = 0x13000000;
    q[0x3F].field_0 = 0x60000040;              // +0x3F0 DMAtag ret, QWC 0x40
    q[0x3F].field_4 = 0;
    q[0x3F].field_8 = 0;
    q[0x3F].field_C = 0;
    // [C 0x1F4168..0x1F41B0] +0x400..+0x7FF: 0x40 qwords {0, 0, 0x7F, 0}
    for (i = 0; i < 0x40; i++) {
        q[0x40 + i].field_0 = 0;
        q[0x40 + i].field_4 = 0;
        q[0x40 + i].field_8 = 0x7F;
        q[0x40 + i].field_C = 0;
    }
    rwDMAFlushAGate.field_0 = 0x30000021;      // DMAtag ref QWC 0x21, ADDR G; FLUSH, FLUSHA
    rwDMAFlushAGate.field_4 = rwDMAVIFGate;
    rwDMAFlushAGate.field_8 = 0x11000000;
    rwDMAFlushAGate.field_C = 0x13000000;
    rwDMAFlushAGateFlush.field_0 = 0x30000022; // DMAtag ref QWC 0x22, ADDR G; FLUSH, FLUSHA
    rwDMAFlushAGateFlush.field_4 = rwDMAVIFGate;
    rwDMAFlushAGateFlush.field_8 = 0x11000000;
    rwDMAFlushAGateFlush.field_C = 0x13000000;
    rwDMAAFlush.field_0 = 0x10000000;          // DMAtag cnt QWC 0; FLUSH, FLUSHA
    rwDMAAFlush.field_4 = 0;
    rwDMAAFlush.field_8 = 0x11000000;
    rwDMAAFlush.field_C = 0x13000000;
    rwDMAANull.field_0 = 0x10000000;           // DMAtag cnt QWC 0
    rwDMAANull.field_4 = 0;
    rwDMAANull.field_8 = 0;
    rwDMAANull.field_C = 0;
    SyncDCache((void *)rwDMAVIFGate, (void *)(rwDMADmaBufs.field_0 * 2 + rwDMAVIFGate + 0x7FF));
    rwDMAPktBase = 0;
    _rwDMA_local_rwDMAPktPtr = 0x70000000;
    _rwDMA_local_rwDMAAsyncPktPtr = 0;
    rwSPRBasePtr = 0;
    rwSPRBaseInMem = 0;
    rwPendingBase = 0;
    _rwDMAAsyncPktPtr[3] = 0;
    _rwDMAPktPtr = 0;
    rwDMADispatchPtr[0] = 0;
    return 1;
}

// [C] _rwDMAUnhook, then rwDMAVIFGate = 0; a buffer the driver allocated (rwDMAbufSize == 0) is freed; rwDMAbuf,
// rwDMAbufSize, rwDMAHYieldFunc and rwDMAMinVsyncCnt are cleared. No defined result.
// 0x001F4578
extern "C" void _rwDMADmaClose(void)
{
    _rwDMAUnhook();
    rwDMAVIFGate = 0;
    if (rwDMAbufSize == 0) {
        ourGlobals.field_138((void *)rwDMAbuf);
    }
    rwDMAbuf = 0;
    rwDMAbufSize = 0;
    rwDMAHYieldFunc = 0;
    rwDMAMinVsyncCnt = 0;
}

// [C] rwDMAHYieldFunc = func (a guest function address or 0). No defined result.
// 0x001F4288
extern "C" void _rwDMAYieldCallbackSet(unsigned int func)
{
    rwDMAHYieldFunc = func;
}

// [C] the byte rwDMAMinVsyncCnt = n ([L] vblanks between flips). No defined result.
// 0x001F4290
extern "C" void func_001F4290(unsigned int n)
{
    rwDMAMinVsyncCnt = (unsigned char)n;
}

// [C] (handwritten) interrupts off (the di loop until Status.EIE reads 0); when transfers are in flight and the last
// dispatched control entry (the byte at rwDMADispatching - 8) is a 0x4F callback, the in-flight count is cleared and
// rwDMAHandler(-1) dispatches on; interrupts back on. Returns 1 when it restarted, else 0.
// 0x001F4298
extern "C" int _rwDMACallbackRestart(void)
{
    do {
        b3hw_di();
        // [C] sync.p
    } while (b3hw_eie() != 0);
    if (rwDMAPktBase != 0 && *(unsigned char *)(rwDMADispatching - 8) == 0x4F) {
        rwDMAPktBase = 0;
        rwDMAHandler(-1);
        b3hw_ei();
        return 1;
    }
    b3hw_ei();
    return 0;
}

// [C] the game's DMA buffer before the engine starts: refused (returns 0) once the DMA layer is open (rwDMAVIFGate
// set) or when buf is not 0x80-aligned. A nonzero buf becomes rwDMAbuf; rwDMAbufSize = size and rwDMAPURefSize =
// nPURefs (its low 16 bits; 0 keeps the current value). Returns 1. B3 calls it at 0x00227F88 with nPURefs 0x180.
// 0x001F45C0
extern "C" int _rwDMAPreAlloc(unsigned int size, unsigned int nPURefs, unsigned int buf)
{
    unsigned int n = nPURefs & 0xFFFF;

    if (n == 0) {
        n = rwDMAPURefSize;
    }
    if (rwDMAVIFGate != 0) {
        return 0;
    }
    if (buf != 0) {
        if ((buf & 0x7F) != 0) {
            return 0;
        }
        rwDMAbuf = buf;
    }
    rwDMAbufSize = size;
    rwDMAPURefSize = (unsigned short)n;
    return 1;
}

// ---------------------------------------------------------------------------------------------------------------
// boot2 additions (track rw): the GIF packet opener, the high allocator, the image-upload packet and the PU-ref ring.
// Same conventions as above: the `bc0f` spins on DMA channel 8 are in `#if 0`; stores through the uncached aliases
// (0x20000000 / 0x30000000 | RAM address) are plain stores with the alias bits removed (the test is
// (a & 0xE0000000) == 0x20000000: the scratchpad 0x70000000 is not an alias).

extern unsigned int rwDMACurrentBuf;              // [C] 0x004E280C: [L] the current upload buffer address (a word here)
extern "C" int _rwDMAOpenVIFPkt(unsigned int type, unsigned int nQW);
extern "C" int _rwDMAOpenGIFPkt(unsigned int nQW);
extern "C" unsigned int _rwDMADMAPktAllocHigh(unsigned int size, unsigned int flag);

// [C] (handwritten) like _rwDMAOpenVIFPkt for the packet kind 0x102 (rwDMAAsyncPktRealBase == 0x102: a GIF packet that
// carries its own 4-qword scratchpad header). Reserves nQW qwords; the counter is increased by one per pass (and
// taken back before a retry after a buffer swap). An open 0x102 packet is extended when it fits (flushing the
// scratchpad first when more than 0x2000 bytes would be in it). Otherwise, with an async cursor
// (_gOldRasterShowRasterFunc) present, an open async header is closed (its DMAtag / VIF DIRECT qword filled in) and four
// header qwords are written at the scratchpad cursor {DMAtag end QWC 3, VIF FLUSH, VIF DIRECT 2; GIFtag NLOOP 1 EOP
// NREG 1 A+D; 0 / 0x7F; VIF 0x13000000 / 0x06000000}, the scratchpad is flushed, and a DMAtag qword {0x20000000,
// next address} is written at the async cursor; rwDMAAsyncPktRealBase = 0x102. When nothing is open (or the packet
// cannot be extended) it is closed and a control slot is reserved first (a 0x7F jump entry to a new 64-aligned block
// when the entry block is exactly full).
// Returns 1, or 0 when even an empty half (rwDMADmaBufs.field_0) is too small ((n << 4) + 0x130).
// 0x001F2AF8
extern "C" int _rwDMAOpenGIFPkt(unsigned int nQW)
{
    unsigned int n;
    unsigned int real;
    unsigned int bytes;
    unsigned int used;
    unsigned int len;
    unsigned int dst;
    unsigned int spr;
    unsigned int p;
    unsigned int cur;
    unsigned int pr;
    unsigned int m;
    unsigned int top;
    unsigned int blk;
    unsigned int base;
    unsigned int v;
    unsigned int g;
    unsigned int old;
    UnkStruct_rwDMAQword *q;

    n = nQW;
    real = rwDMAAsyncPktRealBase;
    for (;;) {
        n = n + 1;
        if (real == 0) goto fresh;
        if (real == 0x102) {
            bytes = n << 4;
            used = farFogPlane - _rwDMA_local_rwDMAPktPtr;
            if (_rwDMA_local_rwDMAAsyncPktPtr + used + bytes < rwDMABufDataTopPtr) {
                if (!(used + bytes < 0x2001)) {
                    // [C 0x1F2B5C] flush the scratchpad first (the same sequence as in _rwDMAOpenVIFPkt)
                    len = 0;
                    if (farFogPlane != 0) {
                        len = used;
                    }
                    if (len != 0) {
                        if (b3hw_read32(0x1000D000) & 0x100) {   // D8_CHCR STR
                            b3hw_write32(0x1000E020, 0x100);     // D_PCR CPC8
                            // sync; sync.p
#if 0
                            // [C 0x1F2B94..0x1F2BAC] `bc0f` loop: spin until DMA channel 8 is done
#endif
                        }
                        spr = _rwDMA_local_rwDMAPktPtr;
                        b3hw_write32(0x1000E010, 0x100);         // D_STAT
                        b3hw_write32(0x1000D080, spr);           // D8_SADR
                        dst = _rwDMA_local_rwDMAAsyncPktPtr;
                        b3hw_write32(0x1000D010, dst);           // D8_MADR
                        b3hw_write32(0x1000D020, len >> 4);      // D8_QWC
                        b3hw_write32(0x1000D000, 0x100);         // D8_CHCR
                        // sync; sync.p
                        p = _rwDMAPktPtr;
                        if (p != 0 && (p & 0xFFFFC000) == 0x70000000) {
                            _rwDMAPktPtr = (dst + (p - spr)) | 0x20000000;
                        }
                        _rwDMA_local_rwDMAAsyncPktPtr = _rwDMA_local_rwDMAAsyncPktPtr + len;
                        farFogPlane = _rwDMA_local_rwDMAPktPtr;
                        if (b3hw_read32(0x1000D000) & 0x100) {
                            b3hw_write32(0x1000E020, 0x100);
                            // sync; sync.p
#if 0
                            // [C 0x1F2C78..0x1F2C90] `bc0f` loop: spin until DMA channel 8 is done
#endif
                        }
                    }
                }
                rwDMABufAsyncTopPtr = _rwDMA_local_rwDMAAsyncPktPtr + bytes + farFogPlane - _rwDMA_local_rwDMAPktPtr;
                return 1;
            }
        }
        // [C 0x1F2CC0]
        if (_gOldRasterShowRasterFunc == 0) goto close;
        used = farFogPlane - _rwDMA_local_rwDMAPktPtr;
        dst = _rwDMA_local_rwDMAAsyncPktPtr;
        bytes = n << 4;
        if (rwDMABufDataTopPtr < ((dst + used + 0xFF) & 0xFFFFFF80) + bytes) goto close;
        cur = farFogPlane;
        if (real & 0x80000000) {
            // [C 0x1F2D08] an async header is open: close it (as in the VIF opener)
            pr = rwDMAPktRealBase;
            m = (((dst - pr) + used) >> 4) - 1;
            if (m != 0) {
                q = (UnkStruct_rwDMAQword *)(pr & ~0x20000000u);
                q->field_0 = m | 0x10000000;    // DMAtag cnt
                q->field_4 = 0;
                q->field_8 = 0;
                q->field_C = m | 0x50000000;    // VIF DIRECT
            } else {
                _rwDMA_local_rwDMAAsyncPktPtr = dst - 0x10;
            }
            rwDMAPktRealBase = 0;
            cur = farFogPlane;
        }
        // [C 0x1F2D60] the four header qwords at the scratchpad cursor; the cursor ends 0x40 above
        q = (UnkStruct_rwDMAQword *)cur;
        q->field_0 = 0x70000003;        // DMAtag end, QWC 3
        q->field_4 = 0;
        q->field_8 = 0x11000000;        // VIF FLUSH
        q->field_C = 0x50000002;        // VIF DIRECT 2
        farFogPlane = cur + 0x10;
        q = (UnkStruct_rwDMAQword *)(cur + 0x10);
        farFogPlane = cur + 0x20;
        q->field_0 = 0x8001;            // GIFtag NLOOP 1, EOP
        q->field_4 = 0x10000000;        // NREG 1
        q->field_8 = 0xE;               // REGS A+D
        q->field_C = 0;
        q = (UnkStruct_rwDMAQword *)(cur + 0x20);
        q->field_0 = 0;
        q->field_4 = 0;
        q->field_8 = 0x7F;
        q->field_C = 0;
        farFogPlane = cur + 0x30;
        q = (UnkStruct_rwDMAQword *)(cur + 0x30);
        q->field_0 = 0x13000000;
        q->field_4 = 0;
        q->field_8 = 0x06000000;
        q->field_C = 0;
        g = cur + 0x40;
        farFogPlane = g;
        len = 0;
        if (g != 0) {
            len = g - _rwDMA_local_rwDMAPktPtr;
        }
        if (len != 0) {
            if (b3hw_read32(0x1000D000) & 0x100) {
                b3hw_write32(0x1000E020, 0x100);
                // sync; sync.p
#if 0
                // [C 0x1F2E20..0x1F2E38] `bc0f` loop: spin until DMA channel 8 is done
#endif
            }
            spr = _rwDMA_local_rwDMAPktPtr;
            b3hw_write32(0x1000E010, 0x100);
            b3hw_write32(0x1000D080, spr);
            dst = _rwDMA_local_rwDMAAsyncPktPtr;
            b3hw_write32(0x1000D010, dst);
            b3hw_write32(0x1000D020, len >> 4);
            b3hw_write32(0x1000D000, 0x100);
            // sync; sync.p
            p = _rwDMAPktPtr;
            if (p != 0 && (p & 0xFFFFC000) == 0x70000000) {
                _rwDMAPktPtr = (dst + (p - spr)) | 0x20000000;
            }
            _rwDMA_local_rwDMAAsyncPktPtr = _rwDMA_local_rwDMAAsyncPktPtr + len;
            farFogPlane = _rwDMA_local_rwDMAPktPtr;
            if (b3hw_read32(0x1000D000) & 0x100) {
                b3hw_write32(0x1000E020, 0x100);
                // sync; sync.p
#if 0
                // [C 0x1F2F08..0x1F2F20] `bc0f` loop: spin until DMA channel 8 is done
#endif
            }
        }
        // [C 0x1F2F28] the DMAtag qword at the async cursor (_gOldRasterShowRasterFunc): {0x20000000, next} where next =
        // the 128-aligned address above the data - 0x10
        v = ((_rwDMA_local_rwDMAAsyncPktPtr + 0xCF) & 0xFFFFFF80) - 0x10;
        old = _gOldRasterShowRasterFunc;
        cur = _rwDMA_local_rwDMAPktPtr;
        farFogPlane = v;
        q = (UnkStruct_rwDMAQword *)(((old & 0xE0000000) == 0x20000000) ? (old & 0x0FFFFFFF) : old);
        q->field_0 = 0x20000000;
        q->field_4 = v;
        q->field_8 = 0;
        q->field_C = 0;
        rwDMABufAsyncTopPtr = v + bytes;
        rwDMAAsyncPktRealBase = 0x102;
        _rwDMA_local_rwDMAAsyncPktPtr = v;
        farFogPlane = cur;
        return 1;

    close:
        _rwDMAClosePkt();
    fresh:
        // [C 0x1F2F8C] a control slot: when the entry block is exactly full (rwDMABufTopPtr == rwDMABufDataPtr) a 0x7F
        // jump entry to a new 64-aligned block at the data position, if 0x80 bytes fit; else none is reserved
        top = rwDMABufTopPtr;
        blk = rwDMABufAsyncTopPtr;
        if (top == rwDMABufDataPtr) {
            blk = (rwDMABufAsyncTopPtr + 0x3F) & 0xFFFFFFC0;
            if (blk + 0x80 < rwDMABufDataTopPtr) {
                ((unsigned int *)top)[1] = blk;
                rwDMABufTopPtr = blk;
                rwDMABufDataPtr = blk + 0x78;
                ((unsigned int *)top)[0] = 0x7F;
                blk = blk + 0x80;
                rwDMABufAsyncTopPtr = blk;
            } else {
                blk = 0;
            }
        }
        if (blk != 0) {
            base = (blk + 0x8F) & 0xFFFFFF80;
            if (!(rwDMABufDataTopPtr < base + ((n - 1) << 4))) {
                base = base - 0x10;
                rwDMABufAsyncTopPtr = base + (n << 4);
                farFogPlane = _rwDMA_local_rwDMAPktPtr;
                rwDMAAsyncPktRealBase = 0x102;
                rwDMABufCtrlTopPtr = base;
                _rwDMA_local_rwDMAAsyncPktPtr = base;
                return 1;
            }
        }
        if (rwDMADmaBufs.field_0 < (n << 4) + 0x130) {
            return 0;
        }
        n = n - 1;
        _rwDMAForceBufferSwap();
        real = rwDMAAsyncPktRealBase;
    }
}

// [C] (handwritten) allocates a block at the top of the current half's data area (growing downwards): when flag != 0
// the scratchpad is flushed first (as in _rwDMAClosePkt) and the DMA channel 8 is waited for (D8_CHCR STR -> D_PCR CPC8
// + spin, once more after a flush). The size is rounded up to 0x80 when it is above 0x7F, else to 0x40; the new top =
// (rwDMABufDataTopPtr & ~mask) - size. When that is still above rwDMABufAsyncTopPtr, rwDMADmaBufSize = the old top,
// rwDMABufDataTopPtr = the new top, and the new top is returned. Otherwise, when the half is big enough
// (rwDMADmaBufs.field_0 >= size + (AsyncTop - local async pkt + 0x9F & ~0x7F) + 0x80), the open packet is closed (the
// packet kind read before), the buffers swapped if the control entry list is not at the start of the current half, and
// a packet of the same kind reopened (_rwDMAOpenGIFPkt for 0x102, else _rwDMAOpenVIFPkt(kind & bit 31, qwords)), and it
// retries; when the half is too small it returns 0.
// 0x001F3768
extern "C" unsigned int _rwDMADMAPktAllocHigh(unsigned int size, unsigned int flag)
{
    unsigned int mask;
    unsigned int a3;
    unsigned int len;
    unsigned int spr;
    unsigned int dst;
    unsigned int p;
    unsigned int top;
    unsigned int async;
    unsigned int need;
    unsigned int kind;

    for (;;) {
        if (flag != 0) {
            len = 0;
            if (farFogPlane != 0) {
                len = farFogPlane - _rwDMA_local_rwDMAPktPtr;
            }
            a3 = len;
            if (a3 != 0) {
                if (b3hw_read32(0x1000D000) & 0x100) {   // D8_CHCR STR
                    b3hw_write32(0x1000E020, 0x100);     // D_PCR CPC8
                    // sync; sync.p
#if 0
                    // [C 0x1F37D0..0x1F37E8] `bc0f` loop: spin until DMA channel 8 is done
#endif
                }
                spr = _rwDMA_local_rwDMAPktPtr;
                b3hw_write32(0x1000E010, 0x100);         // D_STAT
                b3hw_write32(0x1000D080, spr);           // D8_SADR
                dst = _rwDMA_local_rwDMAAsyncPktPtr;
                b3hw_write32(0x1000D010, dst);           // D8_MADR
                b3hw_write32(0x1000D020, a3 >> 4);       // D8_QWC
                b3hw_write32(0x1000D000, 0x100);         // D8_CHCR
                // sync; sync.p
                p = _rwDMAPktPtr;
                if (p != 0 && (p & 0xFFFFC000) == 0x70000000) {
                    _rwDMAPktPtr = (dst + (p - spr)) | 0x20000000;
                }
                _rwDMA_local_rwDMAAsyncPktPtr = _rwDMA_local_rwDMAAsyncPktPtr + a3;
                farFogPlane = _rwDMA_local_rwDMAPktPtr;
                if (b3hw_read32(0x1000D000) & 0x100) {
                    b3hw_write32(0x1000E020, 0x100);
                    // sync; sync.p
#if 0
                    // [C 0x1F38B8..0x1F38D0] `bc0f` loop: spin until DMA channel 8 is done
#endif
                }
            }
            if (b3hw_read32(0x1000D000) & 0x100) {       // [C 0x1F38D8] D8_CHCR STR
                b3hw_write32(0x1000E020, 0x100);         // D_PCR CPC8
                // sync; sync.p
#if 0
                // [C 0x1F3904..0x1F391C] `bc0f` loop: spin until DMA channel 8 is done
#endif
            }
        }
        mask = 0x7F;
        if (!(size > 0x7F)) {
            mask = 0x3F;
        }
        top = rwDMABufDataTopPtr;
        async = rwDMABufAsyncTopPtr;
        size = (size + mask) & ~mask;
        need = (top & ~mask) - size;
        if (async < need) {
            rwDMADmaBufSize = top;
            rwDMABufDataTopPtr = need;
            return need;
        }
        need = ((async - _rwDMA_local_rwDMAAsyncPktPtr + 0x9F) & 0xFFFFFF80);
        if (rwDMADmaBufs.field_0 < size + need + 0x80) {
            return 0;
        }
        kind = rwDMAAsyncPktRealBase;
        _rwDMAClosePkt();
        if (rwPendingCtrlPtr[rwDMADmaBufs.field_4] != rwDMABufTopPtr) {
            _rwDMAForceBufferSwap();
        }
        if (kind == 0x102) {
            _rwDMAOpenGIFPkt(need >> 4);
        } else {
            _rwDMAOpenVIFPkt(kind & 0x80000000, need >> 4);
        }
    }
}

// [C] (handwritten) queues an image upload of nQW qwords after a 2-qword VIF packet: opens a VIF packet (0, 2); if the
// async cursor exists and the current upload buffer (rwDMACurrentBuf) has room for (nQW + 4) * 16 bytes beyond it, the
// data goes at cursor + 0x10 with the 0x30000000 / 0x20000000 bits dropped; else the data area is allocated with
// _rwDMADMAPktAllocHigh((nQW + 3) * 16, 0) (0 -> return 0) and rwDMACurrentBuf = it | 0x30000000. Then: with an async
// cursor, three qwords are written around it {VIF/DMA tag 0x50000003 | (gate + 0x3F0) << 32; 0x40 / 0x10000000, 0xE;
// {0x20000000, buffer}}, the cursor advanced by 0x10 and then 0x20; else rwDMABufCtrlPtr = buffer. The cursor is then
// set to buffer | 0x30000000. With flag != 0 and a packet pointer (_rwDMAPktPtr): if it is not in the scratchpad the DMA
// channel 8 is waited for, then rwDMAFlushAGate is written at the packet pointer and rwDMAAFlush at the scratchpad
// cursor (+0x10); else (flag 0, or no packet pointer) rwDMAFlushAGateFlush at the cursor (+0x10). Finally rwDMAANull is
// written at the cursor, the packet pointer set to it and the cursor advanced by 0x10. Returns 1.
// 0x001F3A10
extern "C" int _rwDMAAddImageUpload(unsigned int flag, unsigned int nQW)
{
    unsigned int buf;
    unsigned int old;
    unsigned int a;
    UnkStruct_rwDMAQword *q;
    UnkStruct_rwDMAQword *src;

    if (!_rwDMAOpenVIFPkt(0, 2)) {
        return 0;
    }
    old = _gOldRasterShowRasterFunc;
    if (old != 0 && !((rwDMACurrentBuf - old) < ((nQW + 4) << 4))) {
        buf = (old + 0x10) & 0xCFFFFFFF;
    } else {
        buf = _rwDMADMAPktAllocHigh((nQW + 3) << 4, 0);
        if (buf == 0) {
            return 0;
        }
        rwDMACurrentBuf = buf | 0x30000000;
    }
    old = _gOldRasterShowRasterFunc;
    if (old != 0) {
        // [C 0x1F3AA8] the cursor is an address with the 0x30000000 alias bits: strip them for the stores
        a = ((old & 0xE0000000) == 0x20000000) ? (old & 0x0FFFFFFF) : old;
        q = (UnkStruct_rwDMAQword *)(a - 0x30);
        q->field_0 = 0x50000003;
        q->field_4 = rwDMAVIFGate + 0x3F0;
        q->field_8 = 0;
        q->field_C = 0;
        q = (UnkStruct_rwDMAQword *)a;
        q->field_0 = 0x40;
        q->field_4 = 0x10000000;
        q->field_8 = 0xE;
        q->field_C = 0;
        _gOldRasterShowRasterFunc = old + 0x10;
        q = (UnkStruct_rwDMAQword *)(a + 0x10);
        q->field_0 = 0x20000000;
        q->field_4 = buf;
        q->field_8 = 0;
        q->field_C = 0;
        _gOldRasterShowRasterFunc = old + 0x20;
    } else {
        rwDMABufCtrlPtr = buf;
    }
    _gOldRasterShowRasterFunc = buf | 0x30000000;
    if (flag != 0 && _rwDMAPktPtr != 0) {
        a = _rwDMAPktPtr;
        if ((a & 0xFFFFC000) != 0x70000000) {
            if (b3hw_read32(0x1000D000) & 0x100) {       // D8_CHCR STR
                b3hw_write32(0x1000E020, 0x100);         // D_PCR CPC8
                // sync; sync.p
#if 0
                // [C 0x1F3B64..0x1F3B7C] `bc0f` loop: spin until DMA channel 8 is done
#endif
            }
        }
        a = _rwDMAPktPtr;
        src = &rwDMAFlushAGate;
        q = (UnkStruct_rwDMAQword *)(((a & 0xE0000000) == 0x20000000) ? (a & 0x0FFFFFFF) : a);
        *q = *src;
        q = (UnkStruct_rwDMAQword *)farFogPlane;
        *q = rwDMAAFlush;
        farFogPlane = farFogPlane + 0x10;
    } else {
        q = (UnkStruct_rwDMAQword *)farFogPlane;
        *q = rwDMAFlushAGateFlush;
        farFogPlane = farFogPlane + 0x10;
    }
    a = farFogPlane;
    q = (UnkStruct_rwDMAQword *)a;
    _rwDMAPktPtr = a;
    *q = rwDMAANull;
    farFogPlane = a + 0x10;
    return 1;
}

// [C] (handwritten) appends the PU reference (a0) to the ring rwDMADispatchEnd[D_004E284A] and advances the end index
// (D_004E284A; wraps to 0 at rwDMAPURefSize). When the ring is then full (end == rwDMAWaited) the end index is advanced
// by half the ring size (wrapping by subtracting the size), the open packet closed (_rwDMAClosePkt), then
// rwDMAWaited = the advanced index and D_004E284A = the old end index. No defined result.
// 0x001F4310
extern "C" void _rwDMAAddPURef(unsigned int ref)
{
    unsigned int end;
    unsigned int size;
    unsigned int next;
    unsigned int waited;
    unsigned int adv;
    unsigned int saved;

    end = D_004E284A;
    ((unsigned int *)rwDMADispatchEnd)[end] = ref;
    size = rwDMAPURefSize;
    next = end + 1;
    D_004E284A = (unsigned short)next;
    if ((next & 0xFFFF) == (size & 0xFFFF)) {
        D_004E284A = 0;
    }
    waited = rwDMAWaited;
    end = D_004E284A;
    if (waited != end) {
        return;
    }
    saved = end;
    adv = waited + ((size & 0xFFFF) >> 1);
    D_004E284A = (unsigned short)adv;
    if (!((adv & 0xFFFF) < (size & 0xFFFF))) {
        D_004E284A = (unsigned short)(adv - size);
    }
    adv = D_004E284A;
    _rwDMAClosePkt();
    rwDMAWaited = (unsigned short)adv;
    D_004E284A = (unsigned short)saved;
}

// [C] (handwritten) flips the pending flip request: with rwDMADispatchPtr[0] set (a flip-requested flag), clears it;
// when entries remain (rwDMADispatching != rwDMAFlipSt) and nothing is in flight (rwDMAPktBase == 0) and no flip is
// pending (_rwDMAAsyncPktPtr[3] == 0), calls rwDMAHandler(-1); then, when the current half's first control entry
// (rwPendingCtrlPtr[rwDMADmaBufs.field_4]) is not the entry write position (rwDMABufTopPtr), swaps the buffers.
// Does nothing when the flag was clear. No defined result.
// 0x001F43B0
extern "C" void func_001F43B0(void)
{
    if (rwDMADispatchPtr[0] == 0) {
        return;
    }
    rwDMADispatchPtr[0] = 0;
    if (rwDMADispatching != rwDMAFlipSt && rwDMAPktBase == 0 && _rwDMAAsyncPktPtr[3] == 0) {
        rwDMAHandler(-1);
    }
    if (rwPendingCtrlPtr[rwDMADmaBufs.field_4] != rwDMABufTopPtr) {
        _rwDMAForceBufferSwap();
    }
}

// [C] (handwritten) queues a display flip: opens a VIF packet (0, 0x18 qwords) and writes 24 qwords at the cursor:
// {DMAtag cnt QWC 0x17, VIF FLUSH, VIF DIRECT 2}, {GIFtag NLOOP 1 EOP NREG 1, A+D}, {0, 0, 0x7F, 0}, {0, 0, 0x13000000,
// 0}, then 20 zero qwords; the cursor moves 0x180 up. If rwDMADispatchPtr[0] is set the buffers are swapped first;
// rwDMADispatchPtr[0] = 1. The flip data (a0) is queued with _rwDMAAddPkt(flip, 0x41) when bit 0 of the frame bit
// (a1 & 0xFF) is set, else 0x40. Then, with interrupts disabled (the di / EIE loop), _rwDMAAsyncPktPtr[2] is incremented
// and, when nothing is in flight and no flip is pending, rwDMAHandler(-1) is called; interrupts are re-enabled. No
// defined result.
// 0x001F2960
extern "C" void _rwDMAReqFlip(unsigned int flip, unsigned int frameBit)
{
    UnkStruct_rwDMAQword *q;
    unsigned int cur;
    unsigned int i;

    frameBit = frameBit & 0xFF;
    _rwDMAOpenVIFPkt(0, 0x18);
    cur = farFogPlane;
    q = (UnkStruct_rwDMAQword *)cur;
    q->field_0 = 0x10000017;
    q->field_4 = 0;
    q->field_8 = 0x11000000;
    q->field_C = 0x50000002;
    cur = cur + 0x10;
    q = (UnkStruct_rwDMAQword *)cur;
    q->field_0 = 0x8001;
    q->field_4 = 0x10000000;
    q->field_8 = 0xE;
    q->field_C = 0;
    cur = cur + 0x10;
    q = (UnkStruct_rwDMAQword *)cur;
    q->field_0 = 0;
    q->field_4 = 0;
    q->field_8 = 0x7F;
    q->field_C = 0;
    cur = cur + 0x10;
    q = (UnkStruct_rwDMAQword *)cur;
    q->field_0 = 0;
    q->field_4 = 0;
    q->field_8 = 0x13000000;
    q->field_C = 0;
    cur = cur + 0x10;
    for (i = 0; i < 20; i++) {
        q = (UnkStruct_rwDMAQword *)(cur + i * 0x10);
        q->field_0 = 0;
        q->field_4 = 0;
        q->field_8 = 0;
        q->field_C = 0;
    }
    farFogPlane = cur + 0x140;
    if (rwDMADispatchPtr[0] != 0) {
        _rwDMAForceBufferSwap();
    }
    rwDMADispatchPtr[0] = 1;
    if ((frameBit & 1) != 0) {
        _rwDMAAddPkt(flip, 0x41);
    } else {
        _rwDMAAddPkt(flip, 0x40);
    }
    do {
        b3hw_di();
        // sync.p
    } while (b3hw_eie());
    _rwDMAAsyncPktPtr[2] = (unsigned char)(_rwDMAAsyncPktPtr[2] + 1);
    if (rwDMAPktBase == 0 && _rwDMAAsyncPktPtr[3] == 0 && rwDMAPktBase == 0) {
        rwDMAHandler(-1);
    }
    b3hw_ei();
}
