// Sony EE library libkernl (SDK 2.8.0 as retail links it): the file-I/O client (the EE side of the IOP's fileio RPC server, libkernl.s,
// code 0x0010F910-0x00112C7C; data asm/data/sce/libkernl.{data,bss,rodata}.s). Written from the retail asm. Tags: [C] confirmed from the
// asm, [L] likely, [G] guess.
// Shape [C]: one libsif client D_004E7140 (bound to server 0x80000001 by sceFsInit), one request buffer D_004E5A40 (the words
// below), one 8-byte receive buffer D_004E6680 (nonzero first word = the IOP accepted the request), and the reply that carries the
// call's result arrives later as a SIF command (cid 0x80000011, handler _sceFs_Rcv_Intr) in one of two 0x440-byte EE buffers
// (D_004E66C0 + sel * 0x440). Each call: _sceFsWaitS (serialises the callers on _fs_semid), get the iob (a 32-entry table D_004E6F40,
// 16 bytes each: +0 the server's file id, +4 the open flags, 0 = free, 0x10000000 = being opened), fill the request words, CreateSema (a
// per-call semaphore, max 1, init 0), request word 0 = that semaphore, word 1 = the EE address of the result word (the caller's
// stack, sp+0x30), word 2 = 4, sceSifCallRpc (fno per call), then WaitSema on the call's semaphore until the handler's iSignalSema,
// DeleteSema, return the result word. A negative semaphore id in the reply (and in the request, flag 0x8000 = no-wait) is the queued
// no-wait path: the id is kept in _sceFs_q[32] (guarded by _fs_fsq_semid) and the reply handler clears the entry.
// Locals whose address goes into guest memory (the semaphore parameter at sp+0x10, the result word at sp+0x30) are guest-stack frame
// locals at retail's offsets (B3_RETAIL_FRAME / B3_FRAME_LOCAL, decomp2host T27).
// Request layout (D_004E5A40, words; every call sends a prefix):
//   +0x00 semaphore id (negated when queued no-wait)  +0x04 EE address of the result word  +0x08 4
//   open  fno 0 (send 0x418): +0x0C flags & 0x7FFFFFFF, +0x10 mode, +0x14 name (0x400 bytes, NUL-terminated), +0x414 iob index
//   close fno 1 (send 0x14):  +0x0C server fd, +0x10 iob index
//   read  fno 2 (send 0x20):  +0x0C fd, +0x10 buffer address, +0x14 size, +0x18 (not written), +0x1C iob index
//   write fno 3 (send 0x30):  +0x0C fd, +0x10 buffer address, +0x14 size, +0x18 head byte count (<= 0xF), +0x1C head bytes, +0x2C iob index
//   lseek fno 4 (send 0x1C):  +0x0C fd, +0x10 offset, +0x14 whence, +0x18 iob index
//   ioctl fno 5 (send 0x420): +0x0C fd, +0x10 cmd, +0x14 data (0x400 bytes, only when arg != 0), +0x414 = 0, +0x418 = 0, +0x41C 0x400 / 0
//   ioctl2 fno 0x1A (send 0x420): +0x0C fd, +0x10 cmd, +0x14 argument bytes, +0x414 return buffer address, +0x418 return size, +0x41C arg size
//   devctl fno 0x17 (send 0x81C): +0x0C name (0x400), +0x40C argument bytes (0x400), +0x80C cmd, +0x810 arg size, +0x814 return buffer, +0x818 return size
//   init  fno 0xFF (send D_004E5A00, 8: the two EE reply buffer addresses; receive D_004E6680, 8: version "2800" / "....", then 2 = double buffer)
// boot2 (2026-11-06).

#include "b3hw/hw.h"

// [C] the libsif client (sceSifClientData, 0x28 bytes): +0x24 is the bound server (0 until the IOP answers the bind).
struct UnkStruct_SifClient {
    unsigned int field_0;
    unsigned int field_4;
    unsigned int field_8;
    unsigned int field_C;
    unsigned int field_10;
    unsigned int field_14;
    unsigned int field_18;
    unsigned int field_1C;
    unsigned int field_20;
    unsigned int field_24;
};

// [C] the semaphore parameter ([L] ee_sema_t: count, max, init, waiters, attr, option): the asm writes max (+4), init (+8) and
// option (+0x14) only.
struct UnkStruct_SemaParam {
    int field_0;
    int field_4;
    int field_8;
    int field_C;
    unsigned int field_10;
    unsigned int field_14;
};

// [C] one iob (D_004E6F40[32], 16 bytes): +0 the server's file id, +4 the flags (0 = free; 0x10000000 = being opened).
struct UnkStruct_FsIob {
    int field_0;
    int field_4;
    int field_8;
    int field_C;
};

// [C] the two reply buffers' EE addresses sent by sceFsInit (D_004E5A00).
struct UnkStruct_FsBufs {
    unsigned char *field_0;
    unsigned char *field_4;
};

// [C] the power-off callback record (D_004E71C0, also the SIF handler argument of _sceFs_Poff_Intr): +0 the callback, +4 its argument.
struct UnkStruct_FsPoff {
    unsigned int field_0;
    unsigned int field_4;
};

typedef void (*UnkFn_FsHandler)(void *, void *);

// the retail ELF's gp (docs/results/boot1-2026-11-05/rw/sky2_handlers.md) [L]
#define FS_GP 0x004E8670u

// ---- data (.data 0x00484140..; .bss 0x004E5A00..; rodata 0x004B22B8)
extern unsigned char D_0048414C[];          // [C] 0x0048414C: "2800", the version the server must report
extern int _sceFs_q[];                      // [C] 0x00484178: 32 queued no-wait semaphore ids (-1 = free)
extern int ee_retbuf_sel;                   // [C] 0x004841F8: the reply buffer the last reply arrived in (0 / 1)
extern int _fs_init;                        // [C] 0x004841FC
extern int _fs_rcv_bufdbl;                  // [C] 0x00484200: 1 when the server double-buffers its replies
extern int _fs_semid;                       // [C] 0x00484204: serialises the callers
extern int _fs_iob_semid;                   // [C] 0x00484208: guards the iob table
extern int _fs_fsq_semid;                   // [C] 0x0048420C: guards _sceFs_q
extern unsigned int _fswildcard;            // [C] 0x00484210: pointer to "...." (0x004B22B8)
extern UnkStruct_FsBufs D_004E5A00;         // [C] 0x004E5A00: sceFsInit's send buffer (8 bytes)
extern unsigned int D_004E5A08;             // [C] 0x004E5A08: sceIoctl's argument (a result pointer for cmd 1 / 2 / 3)
extern int D_004E5A40[];                    // [C] 0x004E5A40: the request (0xC40 bytes)
extern int D_004E6680[];                    // [C] 0x004E6680: the RPC receive buffer (0x40; the first word = accepted)
extern unsigned char D_004E66C0[];          // [C] 0x004E66C0: the reply buffers (2 x 0x440)
extern unsigned char D_004E66D0[];          // [C] 0x004E66D0: the reply buffers + 0x10 (the result bytes)
extern UnkStruct_FsIob D_004E6F40[];        // [C] 0x004E6F40: the iob table (32 x 16)
extern UnkStruct_SifClient D_004E7140;      // [C] 0x004E7140: the fileio RPC client
extern unsigned char D_004E7168[];          // [C] 0x004E7168: the server's version string (4 bytes)
extern unsigned char D_004E7180[];          // [C] 0x004E7180: the SIF handler argument of _sceFs_Rcv_Intr (unused)
extern UnkStruct_FsPoff D_004E71C0;         // [C] 0x004E71C0: the power-off callback record
extern unsigned int D_004E7200;             // [C] 0x004E7200: the power-off callback's $gp

extern "C" void sceSifInitRpc(unsigned int mode);
extern "C" int sceSifBindRpc(UnkStruct_SifClient *cd, unsigned int id, unsigned int mode);
extern "C" int sceSifCallRpc(UnkStruct_SifClient *cd, unsigned int fno, unsigned int mode, void *send, int ssize,
                             void *recv, int rsize, unsigned int endfunc, unsigned int endpara);
extern "C" int sceSifAddCmdHandler(unsigned int cid, UnkFn_FsHandler handler, unsigned int harg);
extern "C" int CreateSema(UnkStruct_SemaParam *param);
extern "C" int DeleteSema(int id);
extern "C" int WaitSema(int id);
extern "C" int SignalSema(int id);
extern "C" int iSignalSema(int id);
extern "C" int func_00114E60(void);         // [C] DIntr (returns whether interrupts were enabled)
extern "C" int EIntr(void);
extern "C" void func_0010E7D0(void *addr, int size);    // [C] libkernl 0x10E7D0: writes the data cache lines of [addr, addr + size) back
extern "C" void *memcpy(void *dst, const void *src, unsigned int n);
extern "C" void *memset(void *dst, int c, unsigned int n);
extern "C" int memcmp(const void *a, const void *b, unsigned int n);

extern "C" int sceFsInit(void);
extern "C" void _sceFsIobSemaMK(void);
extern "C" int _sceFsWaitS(int unused);
extern "C" int _sceFsSigSema(void);
extern "C" void _sceFsSemInit(void);
extern "C" UnkStruct_FsIob *new_iob(void);
extern "C" UnkStruct_FsIob *get_iob(unsigned int fd);
extern "C" int _fs_version(void);
extern "C" void _sceFs_Rcv_Intr(void *pkt, void *harg);
extern "C" void _sceFs_Poff_Intr(void *pkt, void *harg);

// [C] the busy delay after a bind that answered with no server yet: 0x100001 iterations (v0 = 0x100000, addiu -1 until -1).
static inline void fs_delay(void)
{
    int d;

    for (d = 0x100000; d != -1; d--) {
    }
}

// [C] first call: the two iob / fsq semaphores are created (CreateSema {+4 = 1, +8 = 1, +0x14 = 0} twice with the same parameter).
// Nothing when _fs_iob_semid is already set ([C] the asm tests only _fs_iob_semid).
// 0x0010F910
extern "C" void _sceFsIobSemaMK(void)
{
    B3_RETAIL_FRAME(0x40);
    UnkStruct_SemaParam *p = B3_FRAME_LOCAL(UnkStruct_SemaParam, 0);

    if (_fs_iob_semid == -1) {
        p->field_14 = 0;
        p->field_4 = 1;
        p->field_8 = 1;
        _fs_iob_semid = CreateSema(p);
        _fs_fsq_semid = CreateSema(p);
    }
}

// [C] a free iob: _sceFsIobSemaMK, WaitSema(iob semaphore), the first entry whose flags are 0 gets flags 0x10000000 (stored before
// the SignalSema, in its delay slot) and is returned; none free: SignalSema, 0.
// 0x0010F970
extern "C" UnkStruct_FsIob *new_iob(void)
{
    UnkStruct_FsIob *e;

    _sceFsIobSemaMK();
    WaitSema(_fs_iob_semid);
    for (e = D_004E6F40; e < D_004E6F40 + 0x20; e++) {
        if (e->field_4 == 0) {
            e->field_4 = 0x10000000;
            SignalSema(_fs_iob_semid);
            return e;
        }
    }
    SignalSema(_fs_iob_semid);
    return 0;
}

// [C] the iob of fd: _sceFsIobSemaMK, WaitSema; fd >= 0x20 (unsigned): SignalSema, 0. Else SignalSema, &D_004E6F40[fd].
// 0x0010F9F8
extern "C" UnkStruct_FsIob *get_iob(unsigned int fd)
{
    _sceFsIobSemaMK();
    WaitSema(_fs_iob_semid);
    if (fd >= 0x20) {
        SignalSema(_fs_iob_semid);
        return 0;
    }
    SignalSema(_fs_iob_semid);
    return &D_004E6F40[fd];
}

// [C] the SIF command handler of the server's replies (cid 0x80000011; interrupt context): sel = the packet's +0xC when the server
// double-buffers (_fs_rcv_bufdbl) else 0; ee_retbuf_sel = sel; the reply is the buffer D_004E66C0 + sel * 0x440 (accessed through the
// uncached alias in the asm: the same RAM). Header words (copied to the stack): [0] the call's semaphore id, [1] a reply type, [2] the
// EE result address, [3] its size. Semaphore id >= 0: memcpy(result address, buffer + 0x10, size). Then by type ([1] - 2 < 0x19 jump
// table, jtbl_004B2250): 2 (read) two byte blocks (counts +0x14 / +0x18, destinations +0x1C / +0x20, bytes +0x24 / +0x64); 0xB copies
// 144 (0x144) bytes (+0x18) to the pointer at +0x14 when the result (*[2]) is positive; 0xC copies 0x40 bytes (+0x18) to the pointer at
// +0x14; 0x17 / 0x19 / 0x1A copy min(len at +0x18, 0x400) bytes from +0x1C to the pointer at +0x14. Other types nothing. Then: a
// non-negative id: iSignalSema(id); a negative id (the queued no-wait path): the entry -id of _sceFs_q (the first one equal, slot 0
// first) is cleared to -1.
// 0x0010FA68
extern "C" void _sceFs_Rcv_Intr(void *pkt, void *harg)
{
    unsigned int *p = (unsigned int *)pkt;
    unsigned int sel;
    unsigned char *buf;
    unsigned int *w;
    unsigned int h0;
    unsigned int h1;
    unsigned char *h2;
    unsigned int h3;
    int id;
    int i;

    sel = 0;
    if (_fs_rcv_bufdbl != 0) {
        sel = p[3];
    }
    ee_retbuf_sel = (int)sel;
    buf = D_004E66C0 + (int)sel * 0x440;
    w = (unsigned int *)buf;
    h0 = w[0];
    h1 = w[1];
    h2 = (unsigned char *)w[2];
    h3 = w[3];
    if ((int)h0 >= 0) {
        memcpy(h2, buf + 0x10, h3);
    }
    switch (h1) {
    case 2: {
        unsigned int *a3 = (unsigned int *)(buf + 0x14);
        unsigned char *dst;

        if ((int)a3[0] > 0) {
            dst = (unsigned char *)a3[2];
            for (i = 0; i < (int)a3[0]; i++) {
                dst[i] = buf[0x24 + i];
            }
        }
        if ((int)a3[1] > 0) {
            dst = (unsigned char *)a3[3];
            for (i = 0; i < (int)a3[1]; i++) {
                dst[i] = buf[0x64 + i];
            }
        }
        break;
    }
    case 0xB: {
        unsigned char *dst;

        if (*(int *)h2 > 0) {
            dst = (unsigned char *)*(unsigned int *)(buf + 0x14);
            for (i = 0; i < 0x144; i++) {
                dst[i] = buf[0x18 + i];
            }
        }
        break;
    }
    case 0xC: {
        unsigned char *dst = (unsigned char *)*(unsigned int *)(buf + 0x14);

        for (i = 0; i < 0x40; i++) {
            dst[i] = buf[0x18 + i];
        }
        break;
    }
    case 0x17:
    case 0x19:
    case 0x1A: {
        unsigned char *dst = (unsigned char *)*(unsigned int *)(buf + 0x14);
        unsigned int len = *(unsigned int *)(buf + 0x18);

        if (len > 0x400) {
            len = 0x400;
        }
        memcpy(dst, buf + 0x1C, len);
        break;
    }
    default:
        break;
    }
    if ((int)h0 >= 0) {
        iSignalSema((int)h0);
        return;
    }
    id = -(int)h0;
    if (_sceFs_q[0] == id) {
        _sceFs_q[0] = -1;
        return;
    }
    for (i = 1; i < 0x20; i++) {
        if (_sceFs_q[i] == id) {
            _sceFs_q[i] = -1;
            return;
        }
    }
}

// [C] the call lock's semaphore (_fs_semid, max 1 init 1) created once.
// 0x0010FE20
extern "C" void _sceFsSemInit(void)
{
    B3_RETAIL_FRAME(0x40);
    UnkStruct_SemaParam *p = B3_FRAME_LOCAL(UnkStruct_SemaParam, 0);

    if (_fs_semid == -1) {
        p->field_14 = 0;
        p->field_4 = 1;
        p->field_8 = 1;
        _fs_semid = CreateSema(p);
    }
}

// [C] _sceFsSemInit, WaitSema(_fs_semid); returns 0 (the argument, a call id 0..0x1B, is unused).
// 0x0010FE70
extern "C" int _sceFsWaitS(int unused)
{
    _sceFsSemInit();
    WaitSema(_fs_semid);
    return 0;
}

// [C] tail call SignalSema(_fs_semid).
// 0x0010FEA0
extern "C" int _sceFsSigSema(void)
{
    return SignalSema(_fs_semid);
}

// [C] (unnamed; sets the power-off callback, the fileio twin of sceCdPOffCallback) _sceFsWaitS(0x1B); sceFsInit when not initialised;
// interrupts off: swaps D_004E71C0.field_0 with func, D_004E71C0.field_4 = arg, D_004E7200 = $gp; EIntr when they were on; _sceFsSigSema.
// Returns the old callback.
// 0x0010FEB0
extern "C" unsigned int func_0010FEB0(unsigned int func, unsigned int arg)
{
    unsigned int old;
    int en;

    _sceFsWaitS(0x1B);
    if (_fs_init == 0) {
        sceFsInit();
    }
    en = func_00114E60();
    old = D_004E71C0.field_0;
    D_004E71C0.field_4 = arg;
    D_004E71C0.field_0 = func;
    D_004E7200 = FS_GP;
    if (en != 0) {
        EIntr();
    }
    _sceFsSigSema();
    return old;
}

// [C] the power-off command handler (cid 0x80000013; harg = &D_004E71C0): the callback (harg->field_0) when set is called with
// harg->field_4 ($gp = D_004E7200 around the call). Then `sync; ei` (handwritten): interrupts on.
// 0x0010FF58
extern "C" void _sceFs_Poff_Intr(void *pkt, void *harg)
{
    typedef void (*UnkFn_FsPoff)(unsigned int);
    UnkStruct_FsPoff *rec = (UnkStruct_FsPoff *)harg;
    UnkFn_FsPoff cb;

    cb = (UnkFn_FsPoff)rec->field_0;
    if (cb != 0) {
        // [C] s0 = $gp; $gp = D_004E7200 around the call (no $gp in host C)
        cb(rec->field_4);
    }
    // sync
    b3hw_ei();
}

// [C] sceFsInit: sceSifInitRpc(0); the callback record cleared; interrupts off, handlers 0x80000011 (_sceFs_Rcv_Intr, harg
// D_004E7180) and 0x80000013 (_sceFs_Poff_Intr, harg &D_004E71C0) added, back on. Bind D_004E7140 to 0x80000001: a negative result
// returns -1 at once; a zero server: the delay, bind again. Then the iob flags cleared (under the iob semaphore), D_004E5A00 = the two
// reply buffer addresses (D_004E66C0, + 0x440), call fno 0xFF (send D_004E5A00 8 bytes, receive D_004E6680 8 bytes); negative:
// returns 0xFFFEFFFF. Else D_004E7168 = the answer's first word (the version), _fs_init = 1, _fs_rcv_bufdbl = (answer word 1 == 2),
// returns 0.
// 0x0010FFA0
extern "C" int sceFsInit(void)
{
    int en;
    int i;
    unsigned int ver;
    unsigned int dbl;

    sceSifInitRpc(0);
    D_004E71C0.field_0 = 0;
    D_004E71C0.field_4 = 0;
    en = func_00114E60();
    sceSifAddCmdHandler(0x80000011, _sceFs_Rcv_Intr, (unsigned int)D_004E7180);
    sceSifAddCmdHandler(0x80000013, _sceFs_Poff_Intr, (unsigned int)&D_004E71C0);
    if (en != 0) {
        EIntr();
    }
    for (;;) {
        if (sceSifBindRpc(&D_004E7140, 0x80000001, 0) < 0) {
            return -1;
        }
        if (D_004E7140.field_24 != 0) {
            break;
        }
        fs_delay();
    }
    _sceFsIobSemaMK();
    WaitSema(_fs_iob_semid);
    for (i = 0; i < 0x20; i++) {
        D_004E6F40[i].field_4 = 0;
    }
    SignalSema(_fs_iob_semid);
    D_004E5A00.field_0 = D_004E66C0;
    D_004E5A00.field_4 = D_004E66C0 + 0x440;
    if (sceSifCallRpc(&D_004E7140, 0xFF, 0, &D_004E5A00, 8, D_004E6680, 8, 0, 0) < 0) {
        return 0xFFFEFFFF;
    }
    ver = (unsigned int)D_004E6680[0];      // alias
    *(unsigned int *)D_004E7168 = ver;
    dbl = (unsigned int)D_004E6680[1];
    _fs_init = 1;
    _fs_rcv_bufdbl = (dbl == 2) ? 1 : 0;
    return 0;
}

// [C] the server's version check: 0 (acceptable) when D_004E7168 equals "2800" (D_0048414C) or the wildcard "...." (_fswildcard),
// else (the version "2800" differing from the wildcard, always) 1.
// 0x001101A8
extern "C" int _fs_version(void)
{
    int r = 0;

    if (memcmp(D_004E7168, D_0048414C, 4) != 0) {
        if (memcmp(D_004E7168, (const void *)_fswildcard, 4) != 0) {
            r = (memcmp(D_0048414C, (const void *)_fswildcard, 4) != 0) ? 1 : 0;
        }
    }
    return r;
}

// [C] _fs_init = 0, the version string cleared (memset 4); returns 0.
// 0x00110238
extern "C" int sceFsReset(void)
{
    _fs_init = 0;
    memset(D_004E7168, 0, 4);
    return 0;
}

// [C] sceOpen(name, flags, mode): _sceFsWaitS; sceFsInit when needed; _fs_version != 0: _sceFsSigSema, 0xFFFEFFFC. new_iob zero:
// _sceFsSigSema, -0x13. The name copied to the request +0x14 (up to 0x400 bytes, the last forced NUL when the name fills them), the
// request filled (+0xC flags & 0x7FFFFFFF, +0x10 mode, +0x414 iob index), call fno 0 (send 0x418, receive D_004E6680 4); negative:
// DeleteSema, _sceFsSigSema, -0xB. Else the accept word read (alias), _sceFsSigSema; 0: DeleteSema, -0xB. WaitSema, DeleteSema; a
// negative result: the iob freed (flags 0, under the iob semaphore), the result returned. Else (iob semaphore held) the iob's flags
// |= flags and its +0 = the result (both stored before SignalSema), returns the iob index. [C] the varargs spill (a2..t3 to the
// stack) is not written here. [G] the third parameter is the mode (the asm sends the saved a2 at +0x10).
// 0x00110270
extern "C" int sceOpen(const char *name, int flags, int mode)
{
    B3_RETAIL_FRAME(0x150);
    UnkStruct_SemaParam *sp = B3_FRAME_LOCAL(UnkStruct_SemaParam, 0x10);
    int *res = B3_FRAME_LOCAL(int, 0x30);
    unsigned char *rq = (unsigned char *)D_004E5A40;
    UnkStruct_FsIob *iob;
    int idx;
    int sema;
    int ok;
    int i;
    unsigned char c;

    _sceFsWaitS(0);
    if (_fs_init == 0) {
        sceFsInit();
    }
    if (_fs_version() != 0) {
        _sceFsSigSema();
        return 0xFFFEFFFC;
    }
    iob = new_iob();
    if (iob == 0) {
        _sceFsSigSema();
        return -0x13;
    }
    c = (unsigned char)name[0];
    rq[0x14] = c;
    i = 0;
    if (c != 0) {
        i = 1;
        while (i < 0x400) {
            c = (unsigned char)name[i];
            rq[0x14 + i] = c;
            if (c == 0) {
                break;
            }
            i++;
        }
    }
    if (i == 0x400) {
        rq[0x413] = 0;
    }
    idx = (int)(iob - D_004E6F40);
    D_004E5A40[3] = flags & 0x7FFFFFFF;
    D_004E5A40[4] = mode;
    sp->field_4 = 1;
    D_004E5A40[0x105] = idx;
    sp->field_8 = 0;
    sp->field_14 = 0;
    sema = CreateSema(sp);
    D_004E5A40[1] = (int)(unsigned int)res;
    D_004E5A40[0] = sema;
    D_004E5A40[2] = 4;
    if (sceSifCallRpc(&D_004E7140, 0, 0, D_004E5A40, 0x418, D_004E6680, 4, 0, 0) < 0) {
        DeleteSema(sema);
        _sceFsSigSema();
        return -0xB;
    }
    ok = D_004E6680[0];                     // alias
    _sceFsSigSema();
    if (ok == 0) {
        DeleteSema(sema);
        return -0xB;
    }
    WaitSema(sema);
    DeleteSema(sema);
    if (*res < 0) {
        WaitSema(_fs_iob_semid);
        iob->field_4 = 0;
        SignalSema(_fs_iob_semid);
        return *res;
    }
    WaitSema(_fs_iob_semid);
    iob->field_4 = iob->field_4 | flags;
    iob->field_0 = *res;
    SignalSema(_fs_iob_semid);
    return idx;
}

// [C] sceClose(fd): get_iob(fd), _sceFsWaitS; not initialised: _sceFsSigSema, -1. No iob or its flags 0: _sceFsSigSema, -9.
// Request +0xC the iob's +0, +0x10 the index; call fno 1 (send 0x14, receive D_004E6680 4); negative: DeleteSema, _sceFsSigSema,
// -0xB. On success the iob's flags are cleared at once (the delay slot of the branch). Accept word zero: DeleteSema, -0xB. WaitSema,
// DeleteSema, the result: negative returned, else 0.
// 0x001104F8
extern "C" int sceClose(unsigned int fd)
{
    B3_RETAIL_FRAME(0xA0);
    UnkStruct_SemaParam *sp = B3_FRAME_LOCAL(UnkStruct_SemaParam, 0x10);
    int *res = B3_FRAME_LOCAL(int, 0x30);
    UnkStruct_FsIob *iob;
    int sema;
    int ok;
    int r;

    iob = get_iob(fd);
    _sceFsWaitS(1);
    if (_fs_init == 0) {
        _sceFsSigSema();
        return -1;
    }
    if (iob == 0 || iob->field_4 == 0) {
        _sceFsSigSema();
        return -9;
    }
    D_004E5A40[3] = iob->field_0;
    sp->field_4 = 1;
    D_004E5A40[4] = (int)(iob - D_004E6F40);
    sp->field_8 = 0;
    sp->field_14 = 0;
    sema = CreateSema(sp);
    D_004E5A40[0] = sema;
    D_004E5A40[1] = (int)(unsigned int)res;
    D_004E5A40[2] = 4;
    if (sceSifCallRpc(&D_004E7140, 1, 0, D_004E5A40, 0x14, D_004E6680, 4, 0, 0) < 0) {
        DeleteSema(sema);
        _sceFsSigSema();
        return -0xB;
    }
    iob->field_4 = 0;
    ok = D_004E6680[0];                     // alias
    _sceFsSigSema();
    if (ok == 0) {
        DeleteSema(sema);
        return -0xB;
    }
    WaitSema(sema);
    DeleteSema(sema);
    r = *res;
    if (r > -1) {
        r = 0;
    }
    return r;
}

// [C] sceLseek(fd, offset, whence): as sceClose up to the request (+0xC fd's server id, +0x10 offset, +0x14 whence, +0x18 index; call
// id 4 for _sceFsWaitS). Flag 0x8000 of the iob's flags (no-wait): under _fs_fsq_semid the request's semaphore id goes into the first
// free _sceFs_q slot (slot 0 first) and the request keeps its negation. Call fno 4 (send 0x1C, receive D_004E6680 4); negative:
// DeleteSema, _sceFsSigSema, -0xB. Accept word zero: DeleteSema, -0xB. No-wait: DeleteSema, 0. Else WaitSema, DeleteSema, the result.
// 0x00110678
extern "C" int sceLseek(unsigned int fd, int offset, int whence)
{
    B3_RETAIL_FRAME(0xC0);
    UnkStruct_SemaParam *sp = B3_FRAME_LOCAL(UnkStruct_SemaParam, 0x10);
    int *res = B3_FRAME_LOCAL(int, 0x30);
    UnkStruct_FsIob *iob;
    int flags;
    int sema;
    int ok;
    int i;

    iob = get_iob(fd);
    _sceFsWaitS(4);
    if (_fs_init == 0) {
        _sceFsSigSema();
        return -1;
    }
    if (iob == 0) {
        _sceFsSigSema();
        return -9;
    }
    flags = iob->field_4;
    if (flags == 0) {
        _sceFsSigSema();
        return -9;
    }
    D_004E5A40[4] = offset;
    D_004E5A40[3] = iob->field_0;
    D_004E5A40[5] = whence;
    D_004E5A40[6] = (int)(iob - D_004E6F40);
    sp->field_4 = 1;
    sp->field_8 = 0;
    sp->field_14 = 0;
    sema = CreateSema(sp);
    D_004E5A40[2] = 4;
    D_004E5A40[1] = (int)(unsigned int)res;
    D_004E5A40[0] = sema;
    if ((flags & 0x8000) != 0) {
        WaitSema(_fs_fsq_semid);
        if (_sceFs_q[0] == -1) {
            _sceFs_q[0] = D_004E5A40[0];
            D_004E5A40[0] = -D_004E5A40[0];
        } else {
            for (i = 1; i < 0x20; i++) {
                if (_sceFs_q[i] == -1) {
                    _sceFs_q[i] = D_004E5A40[0];
                    D_004E5A40[0] = -D_004E5A40[0];
                    break;
                }
            }
        }
        SignalSema(_fs_fsq_semid);
    }
    if (sceSifCallRpc(&D_004E7140, 4, 0, D_004E5A40, 0x1C, D_004E6680, 4, 0, 0) < 0) {
        DeleteSema(sema);
        _sceFsSigSema();
        return -0xB;
    }
    ok = D_004E6680[0];                     // alias
    _sceFsSigSema();
    if (ok == 0) {
        DeleteSema(sema);
        return -0xB;
    }
    if ((flags & 0x8000) != 0) {
        DeleteSema(sema);
        return 0;
    }
    WaitSema(sema);
    DeleteSema(sema);
    return *res;
}

// [C] sceRead(fd, buf, size): as sceLseek (call id 2) with the request +0xC fd's server id, +0x10 buf, +0x14 size, +0x1C iob index
// (+0x18 not written), the no-wait queue the same. Flag 0x20000000 clear: func_0010E7D0(buf, size) (the buffer's cache lines written
// back before the IOP DMAs into it); then func_0010E7D0(request, 0x20). Call fno 2 (send 0x20, receive D_004E6680 4), the tail as
// sceLseek (the reply type 2 of _sceFs_Rcv_Intr copies the unaligned head / tail bytes into buf).
// 0x001108B0
extern "C" int sceRead(unsigned int fd, void *buf, int size)
{
    B3_RETAIL_FRAME(0xE0);
    UnkStruct_SemaParam *sp = B3_FRAME_LOCAL(UnkStruct_SemaParam, 0x10);
    int *res = B3_FRAME_LOCAL(int, 0x30);
    UnkStruct_FsIob *iob;
    int flags;
    int sema;
    int ok;
    int i;

    iob = get_iob(fd);
    _sceFsWaitS(2);
    if (_fs_init == 0) {
        _sceFsSigSema();
        return -1;
    }
    if (iob == 0) {
        _sceFsSigSema();
        return -9;
    }
    flags = iob->field_4;
    if (flags == 0) {
        _sceFsSigSema();
        return -9;
    }
    D_004E5A40[3] = iob->field_0;
    sp->field_4 = 1;
    D_004E5A40[7] = (int)(iob - D_004E6F40);
    D_004E5A40[4] = (int)(unsigned int)buf;
    D_004E5A40[5] = size;
    sp->field_8 = 0;
    sp->field_14 = 0;
    sema = CreateSema(sp);
    D_004E5A40[2] = 4;
    D_004E5A40[1] = (int)(unsigned int)res;
    D_004E5A40[0] = sema;
    if ((flags & 0x8000) != 0) {
        WaitSema(_fs_fsq_semid);
        if (_sceFs_q[0] == -1) {
            _sceFs_q[0] = D_004E5A40[0];
            D_004E5A40[0] = -D_004E5A40[0];
        } else {
            for (i = 1; i < 0x20; i++) {
                if (_sceFs_q[i] == -1) {
                    _sceFs_q[i] = D_004E5A40[0];
                    D_004E5A40[0] = -D_004E5A40[0];
                    break;
                }
            }
        }
        SignalSema(_fs_fsq_semid);
    }
    if ((flags & 0x20000000) == 0) {
        func_0010E7D0(buf, size);
    }
    func_0010E7D0(D_004E5A40, 0x20);
    if (sceSifCallRpc(&D_004E7140, 2, 0, D_004E5A40, 0x20, D_004E6680, 4, 0, 0) < 0) {
        DeleteSema(sema);
        _sceFsSigSema();
        return -0xB;
    }
    ok = D_004E6680[0];                     // alias
    _sceFsSigSema();
    if (ok == 0) {
        DeleteSema(sema);
        return -0xB;
    }
    if ((flags & 0x8000) != 0) {
        DeleteSema(sema);
        return 0;
    }
    WaitSema(sema);
    DeleteSema(sema);
    return *res;
}

// [C] sceWrite(fd, buf, size): as sceRead (call id 3) with the request +0xC fd's server id, +0x10 buf, +0x14 size, +0x2C iob index.
// The head: s = buf & 0xF; 0 when aligned else 0x10 - (buf & 0xF); s = min(s, size) (signed); flag 0x20000000 clear:
// func_0010E7D0(buf, size). Request +0x18 = s and its s bytes copied from buf to +0x1C (the IOP DMAs the aligned rest from buf). Call
// fno 3 (send 0x30, receive D_004E6680 4), the tail as sceLseek.
// 0x00110B10
extern "C" int sceWrite(unsigned int fd, void *buf, int size)
{
    B3_RETAIL_FRAME(0xE0);
    UnkStruct_SemaParam *sp = B3_FRAME_LOCAL(UnkStruct_SemaParam, 0x10);
    int *res = B3_FRAME_LOCAL(int, 0x30);
    unsigned char *rq = (unsigned char *)D_004E5A40;
    unsigned char *src = (unsigned char *)buf;
    UnkStruct_FsIob *iob;
    int flags;
    int sema;
    int ok;
    int head;
    int i;

    iob = get_iob(fd);
    _sceFsWaitS(3);
    if (_fs_init == 0) {
        _sceFsSigSema();
        return -1;
    }
    if (iob == 0) {
        _sceFsSigSema();
        return -9;
    }
    flags = iob->field_4;
    if (flags == 0) {
        _sceFsSigSema();
        return -9;
    }
    D_004E5A40[3] = iob->field_0;
    sp->field_4 = 1;
    D_004E5A40[0xB] = (int)(iob - D_004E6F40);
    D_004E5A40[5] = size;
    D_004E5A40[4] = (int)(unsigned int)buf;
    sp->field_8 = 0;
    sp->field_14 = 0;
    sema = CreateSema(sp);
    D_004E5A40[2] = 4;
    D_004E5A40[1] = (int)(unsigned int)res;
    D_004E5A40[0] = sema;
    if ((flags & 0x8000) != 0) {
        WaitSema(_fs_fsq_semid);
        if (_sceFs_q[0] == -1) {
            _sceFs_q[0] = D_004E5A40[0];
            D_004E5A40[0] = -D_004E5A40[0];
        } else {
            for (i = 1; i < 0x20; i++) {
                if (_sceFs_q[i] == -1) {
                    _sceFs_q[i] = D_004E5A40[0];
                    D_004E5A40[0] = -D_004E5A40[0];
                    break;
                }
            }
        }
        SignalSema(_fs_fsq_semid);
    }
    head = (int)((unsigned int)buf & 0xF);
    if (head != 0) {
        head = 0x10 - head;
    }
    if (size < head) {
        head = size;
    }
    if ((flags & 0x20000000) == 0) {
        func_0010E7D0(buf, size);
    }
    D_004E5A40[6] = head;
    for (i = 0; i < head; i++) {
        rq[0x1C + i] = src[i];
    }
    if (sceSifCallRpc(&D_004E7140, 3, 0, D_004E5A40, 0x30, D_004E6680, 4, 0, 0) < 0) {
        DeleteSema(sema);
        _sceFsSigSema();
        return -0xB;
    }
    ok = D_004E6680[0];                     // alias
    _sceFsSigSema();
    if (ok == 0) {
        DeleteSema(sema);
        return -0xB;
    }
    if ((flags & 0x8000) != 0) {
        DeleteSema(sema);
        return 0;
    }
    WaitSema(sema);
    DeleteSema(sema);
    return *res;
}

// [C] sceIoctl(fd, cmd, arg): get_iob(fd), _sceFsWaitS(5); D_004E5A08 = arg; sceFsInit when needed; no iob or flags 0: _sceFsSigSema,
// -9. Request +0x414 = 0, +0x418 = 0. cmd 1: under _fs_fsq_semid *arg = 1 when any _sceFs_q slot is not -1 else 0 (no RPC), return 0
// (after _sceFsSigSema). cmd 2: *(int *)arg = the reply buffer's result word (D_004E66D0 + ee_retbuf_sel * 0x440), return 0. cmd 3:
// *(long long *)arg = its doubleword, return 0. Any other cmd: request +0xC the iob's server id, +0x10 cmd; arg zero: +0x41C = 0;
// else +0x41C = 0x400 and the 0x400 bytes at arg copied to +0x14. Request +0 the semaphore, +4 the result address, +8 4,
// func_0010E7D0(request, 0x420) (request +0 set in its delay slot), call fno 5 (send 0x420, receive D_004E6680 4), then the usual
// tail (negative: DeleteSema, _sceFsSigSema, -0xB; accept word zero: DeleteSema, -0xB; WaitSema, DeleteSema, the result).
// 0x00110DD0
extern "C" int sceIoctl(unsigned int fd, int cmd, void *arg)
{
    B3_RETAIL_FRAME(0xC0);
    UnkStruct_SemaParam *sp = B3_FRAME_LOCAL(UnkStruct_SemaParam, 0x10);
    int *res = B3_FRAME_LOCAL(int, 0x30);
    unsigned char *rq = (unsigned char *)D_004E5A40;
    unsigned char *a = (unsigned char *)arg;
    UnkStruct_FsIob *iob;
    int sema;
    int ok;
    int i;
    int v;

    iob = get_iob(fd);
    _sceFsWaitS(5);
    D_004E5A08 = (unsigned int)arg;
    if (_fs_init == 0) {
        sceFsInit();
    }
    if (iob == 0 || iob->field_4 == 0) {
        _sceFsSigSema();
        return -9;
    }
    D_004E5A40[0x105] = 0;
    D_004E5A40[0x106] = 0;
    if (cmd == 1) {
        WaitSema(_fs_fsq_semid);
        // [C] 1 when any slot is not -1 (slot 0 tested first, then 1..0x1F), else 0
        v = 0;
        if (_sceFs_q[0] != -1) {
            v = 1;
        } else {
            for (i = 1; i < 0x20; i++) {
                if (_sceFs_q[i] != -1) {
                    v = 1;
                    break;
                }
            }
        }
        *(int *)D_004E5A08 = v;
        SignalSema(_fs_fsq_semid);
        _sceFsSigSema();
        return 0;
    }
    if (cmd == 2) {
        *(int *)arg = *(int *)(D_004E66D0 + ee_retbuf_sel * 0x440);
        _sceFsSigSema();
        return 0;
    }
    if (cmd == 3) {
        *(unsigned long long *)arg = *(unsigned long long *)(D_004E66D0 + ee_retbuf_sel * 0x440);
        _sceFsSigSema();
        return 0;
    }
    D_004E5A40[4] = cmd;
    D_004E5A40[3] = iob->field_0;
    if (arg == 0) {
        D_004E5A40[0x107] = 0;
    } else {
        D_004E5A40[0x107] = 0x400;
        for (i = 0; i < 0x400; i++) {
            rq[0x14 + i] = a[i];
        }
    }
    sp->field_14 = 0;
    sp->field_4 = 1;
    sp->field_8 = 0;
    sema = CreateSema(sp);
    D_004E5A40[1] = (int)(unsigned int)res;
    D_004E5A40[2] = 4;
    D_004E5A40[0] = sema;
    func_0010E7D0(D_004E5A40, 0x420);
    if (sceSifCallRpc(&D_004E7140, 5, 0, D_004E5A40, 0x420, D_004E6680, 4, 0, 0) < 0) {
        DeleteSema(sema);
        _sceFsSigSema();
        return -0xB;
    }
    ok = D_004E6680[0];                     // alias
    _sceFsSigSema();
    if (ok == 0) {
        DeleteSema(sema);
        return -0xB;
    }
    WaitSema(sema);
    DeleteSema(sema);
    return *res;
}

// [C] sceIoctl2(fd, cmd, arg, argsize, retbuf, retsize): get_iob(fd), _sceFsWaitS(0x1A); sceFsInit when needed; no iob or flags 0:
// _sceFsSigSema, -9. argsize or retsize above 0x400 (unsigned): _sceFsSigSema, -0x16. arg zero: nothing, else memcpy(request +0x14,
// arg, argsize). Request +0x10 cmd, +0xC the iob's server id, +0x41C argsize, +0x418 retsize, +0x414 retbuf, +0 the semaphore, +4 the
// result address, +8 4; func_0010E7D0(request, 0x420); call fno 0x1A (send 0x420, receive D_004E6680 4), the usual tail. [L] retbuf
// is the EE address the server's reply (type 0x1A, _sceFs_Rcv_Intr) copies the returned data to; kept as an address word.
// 0x00111148
extern "C" int sceIoctl2(unsigned int fd, int cmd, void *arg, unsigned int argsize, unsigned int retbuf, unsigned int retsize)
{
    B3_RETAIL_FRAME(0xD0);
    UnkStruct_SemaParam *sp = B3_FRAME_LOCAL(UnkStruct_SemaParam, 0x10);
    int *res = B3_FRAME_LOCAL(int, 0x30);
    UnkStruct_FsIob *iob;
    int sema;
    int ok;

    iob = get_iob(fd);
    _sceFsWaitS(0x1A);
    if (_fs_init == 0) {
        sceFsInit();
    }
    if (iob == 0 || iob->field_4 == 0) {
        _sceFsSigSema();
        return -9;
    }
    if (argsize > 0x400 || retsize > 0x400) {
        _sceFsSigSema();
        return -0x16;
    }
    if (arg == 0) {
        D_004E5A40[0x107] = 0;
    } else {
        memcpy((unsigned char *)D_004E5A40 + 0x14, arg, argsize);
    }
    D_004E5A40[4] = cmd;
    D_004E5A40[3] = iob->field_0;
    D_004E5A40[0x107] = (int)argsize;
    sp->field_4 = 1;
    sp->field_8 = 0;
    sp->field_14 = 0;
    sema = CreateSema(sp);
    D_004E5A40[0x106] = (int)retsize;
    D_004E5A40[1] = (int)(unsigned int)res;
    D_004E5A40[2] = 4;
    D_004E5A40[0x105] = (int)retbuf;
    D_004E5A40[0] = sema;
    func_0010E7D0(D_004E5A40, 0x420);
    if (sceSifCallRpc(&D_004E7140, 0x1A, 0, D_004E5A40, 0x420, D_004E6680, 4, 0, 0) < 0) {
        DeleteSema(sema);
        _sceFsSigSema();
        return -0xB;
    }
    ok = D_004E6680[0];                     // alias
    _sceFsSigSema();
    if (ok == 0) {
        DeleteSema(sema);
        return -0xB;
    }
    WaitSema(sema);
    DeleteSema(sema);
    return *res;
}

// [C] sceDevctl(name, cmd, arg, argsize, retbuf, retsize): _sceFsWaitS(0x17); sceFsInit when needed. The name copied to the request
// +0xC (up to 0x400 bytes, the last forced NUL when it fills them). argsize or retsize above 0x400: _sceFsSigSema, -0x16. The
// argsize bytes of arg copied to +0x40C (nothing when argsize is 0); +0x810 argsize, +0x80C cmd, +0x818 retsize, +0x814 retbuf, +0
// the semaphore, +4 the result address, +8 4; func_0010E7D0(request, 0x81C); call fno 0x17 (send 0x81C, receive D_004E6680 4), the usual
// tail. [C] the func_0010E7D0 call is made with the request's +0 already set (delay slot).
// 0x00112A48
extern "C" int sceDevctl(const char *name, int cmd, void *arg, unsigned int argsize, unsigned int retbuf, unsigned int retsize)
{
    B3_RETAIL_FRAME(0xE0);
    UnkStruct_SemaParam *sp = B3_FRAME_LOCAL(UnkStruct_SemaParam, 0x10);
    int *res = B3_FRAME_LOCAL(int, 0x30);
    unsigned char *rq = (unsigned char *)D_004E5A40;
    unsigned char *a = (unsigned char *)arg;
    unsigned char c;
    int i;
    int sema;
    int ok;
    unsigned int n;

    _sceFsWaitS(0x17);
    if (_fs_init == 0) {
        sceFsInit();
    }
    c = (unsigned char)name[0];
    rq[0xC] = c;
    i = 0;
    if (c != 0) {
        i = 1;
        while (i < 0x400) {
            c = (unsigned char)name[i];
            rq[0xC + i] = c;
            if (c == 0) {
                break;
            }
            i++;
        }
    }
    if (i == 0x400) {
        rq[0x40B] = 0;
    }
    if (argsize > 0x400 || retsize > 0x400) {
        _sceFsSigSema();
        return -0x16;
    }
    if (argsize != 0) {
        n = 0;
        do {
            rq[0x40C + n] = a[n];
            n++;
        } while (n < argsize);
    }
    D_004E5A40[0x204] = (int)argsize;
    D_004E5A40[0x203] = cmd;
    sp->field_4 = 1;
    sp->field_8 = 0;
    sp->field_14 = 0;
    sema = CreateSema(sp);
    D_004E5A40[0x206] = (int)retsize;
    D_004E5A40[1] = (int)(unsigned int)res;
    D_004E5A40[2] = 4;
    D_004E5A40[0x205] = (int)retbuf;
    D_004E5A40[0] = sema;
    func_0010E7D0(D_004E5A40, 0x81C);
    if (sceSifCallRpc(&D_004E7140, 0x17, 0, D_004E5A40, 0x81C, D_004E6680, 4, 0, 0) < 0) {
        DeleteSema(sema);
        _sceFsSigSema();
        return -0xB;
    }
    ok = D_004E6680[0];                     // alias
    _sceFsSigSema();
    if (ok == 0) {
        DeleteSema(sema);
        return -0xB;
    }
    WaitSema(sema);
    DeleteSema(sema);
    return *res;
}
