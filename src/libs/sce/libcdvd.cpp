// Sony EE library libcdvd (SDK 2.8.0 as retail links it): the EE side of the IOP's CDVD RPC servers (libcdvd.s, data asm/data/sce/libcdvd.{data,bss,rodata}.s,
// code 0x0011E3A0-0x0011FCF8). Written from the retail asm. Tags: [C] confirmed from the asm, [L] likely, [G] guess.
// Four kinds of RPC clients (libsif sceSifClientData, 0x28 bytes): _sceCd_cd_ncmd (N-commands, server 0x80000595, answered in
// D_004858C0 / D_00485940), _sceCd_cd_scmd (S-commands, 0x80000593, send _sceCd_scmdsdata / receive _sceCd_scmdrdata), D_004EB140
// (search file, 0x80000597), D_004EB168 (init, 0x80000592), D_004EB190 (disk ready: 0x8000059A / 0x8000059C). Three semaphores
// (_sceCd_ncmd_semid, _sceCd_scmd_semid, _sceCd_rcmd_semid) serialise the commands (cmd_sem_init creates them, the prechk
// functions PollSema one: a held semaphore fails the call), cb_semid wakes the callback thread (func_0011E4A8).
// The uncached reads / writes through `addr | 0x20000000` (ldl/ldr, lw, sb through the alias) address the same RAM as the cached
// address: written as plain accesses (comment "alias"). The busy delays (0x100001 iterations of nops) are kept as empty loops.
// Two points of the asm the C cannot show: $gp is saved / restored around the user callbacks (D_004EAF44 / D_004EAF4C hold the
// caller's $gp; here the retail ELF's gp 0x004E8670 [L] from the sky2 notes), and the stack words of the semaphore parameter that
// the asm leaves unwritten (written 0 here [G]).
// boot2 (2026-11-06).

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

// [C] the semaphore parameter on cmd_sem_init's stack ([L] ee_sema_t: count, max, init, waiters, attr, option): the asm writes
// max (+4), init (+8) and option (+0x14) only.
struct UnkStruct_SemaParam {
    int field_0;
    int field_4;
    int field_8;
    int field_C;
    unsigned int field_10;
    unsigned int field_14;
};

// [C] the thread parameter at D_004EAF90 (0x30 bytes, [L] ee_thread_t): the asm writes +4 entry, +8 stack, +0xC stack size,
// +0x10 (0), +0x14 initial priority.
struct UnkStruct_ThreadParam {
    unsigned int field_0;
    unsigned int field_4;       // entry (func_0011E4A8)
    unsigned int field_8;       // stack
    unsigned int field_C;       // stack size
    unsigned int field_10;
    unsigned int field_14;      // initial priority
    unsigned int field_18;
    unsigned int field_1C;
    unsigned int field_20;
    unsigned int field_24;
    unsigned int field_28;
    unsigned int field_2C;
};

// [C] the packet of the SIF command handler func_0011E648: two byte blocks, each (count, destination): count1 +0 / dest1 +8 with
// the bytes at +0x10, count2 +4 / dest2 +0xC with the bytes at +0x50.
struct UnkStruct_CdCbPacket {
    int field_0;
    int field_4;
    unsigned char *field_8;
    unsigned char *field_C;
    unsigned char field_10[0x40];
    unsigned char field_50[1];
};

// [C] the search-file request at D_004EAFC0 (0x12C bytes sent; its first 0x24 bytes are the result sceCdLayerSearchFile copies
// out): the file name (up to 0x100 bytes, NUL-terminated) at +0x24, a pointer to the request itself at +0x124, the layer at +0x128.
struct UnkStruct_CdSearchReq {
    unsigned char field_0[0x24];
    unsigned char field_24[0x100];
    struct UnkStruct_CdSearchReq *field_124;
    int field_128;
};

// [C] sceCdSearchFile's result (0x24 bytes): +8 is printed as a string by the debug trace, +4 and +0 as numbers ([G] the
// name, the LSN and the size).
struct UnkStruct_CdFile {
    unsigned int field_0;
    unsigned int field_4;
    unsigned char field_8[0x1C];
};

// the retail ELF's gp (docs/results/boot1-2026-11-05/rw/sky2_handlers.md) [L]
#define CD_GP 0x004E8670u

// ---- data (.data 0x00485840..; .bss 0x004EAF40..)
extern int SCE_CD_debug;                    // [C] 0x00485850: debug trace level (> 0: scePrintf traces)
extern int cb_thid;                         // [C] 0x00485854: the callback thread (0 = none)
extern int scmd_sema_keep_cmd;              // [C] 0x00485858: the S-command holding the semaphore
extern int ncmd_sema_keep_cmd;              // [C] 0x0048585C: the N-command holding the semaphore
extern int cb_semid;                        // [C] 0x00485860: wakes the callback thread
extern int Init_seq;                        // [C] 0x00485864: 1 while sceCdInit / PowerOffCB run
extern int _sceCd_ncmd_semid;               // [C] 0x00485868
extern int _sceCd_scmd_semid;               // [C] 0x0048586C
extern int _sceCd_rcmd_semid;               // [C] 0x00485870
extern int _sceCd_c_cb_sem;                 // [C] 0x00485874: nonzero while a callback is pending
extern int _sceCd_ee_read_mode;             // [C] 0x00485878
extern int _ncmd_bind;                      // [C] 0x0048587C: -1 until the N-command client is bound
extern int _icmd_bind;                      // [C] 0x00485880
extern int _sf_bind;                        // [C] 0x00485884
extern int _scmd_dr_bind;                   // [C] 0x00485888
extern int _scmd_bind;                      // [C] 0x0048588C
extern int _it_bind;                        // [C] 0x00485890
extern int D_00485894;                      // [C] 0x00485894: c_cnt.66, the count of sceCdInit calls
extern int sceCdCbfunc_num;                 // [C] 0x00485898
extern int sceCdCbfunc_number;              // [C] 0x0048589C
extern unsigned int D_004858C0[];           // [C] 0x004858C0: the N-command answer of fno 0xE (sceCdNcmdDiskReady)
extern unsigned int D_00485940[];           // [C] 0x00485940: the N-command answer table of func_0011F168 (0x8002)
extern UnkStruct_SifClient _sceCd_cd_ncmd;  // [C] 0x00486A10
extern unsigned int _sceCd_scmdrdata[];     // [C] 0x00486A40: S-command answers (0x420)
extern unsigned int _sceCd_scmdsdata[];     // [C] 0x00486E80: S-command requests (0x408)
extern unsigned int _sceCd_rcmdrdata[];     // [C] 0x004872C0: the disk-ready answer (0x100)
extern UnkStruct_SifClient _sceCd_cd_scmd;  // [C] 0x004873C0
extern int rpc_mmode;                       // [C] 0x004873E8: sceCdMmode's function number
extern int outcnt_mmode;                    // [C] 0x004873EC: its send size
extern int incnt_mmode;                     // [C] 0x004873F0: its receive size
extern unsigned int D_004EAF40;             // [C] 0x004EAF40: the user callback of sceCdCallback (code address)
extern unsigned int D_004EAF44;             // [C] 0x004EAF44: its $gp
extern unsigned int D_004EAF48;             // [C] 0x004EAF48: the power-off callback (code address)
extern unsigned int D_004EAF4C;             // [C] 0x004EAF4C: its $gp
extern unsigned int D_004EAF50;             // [C] 0x004EAF50: its argument
extern int D_004EAF54;                      // [C] 0x004EAF54: cleared when the callback thread exits
extern int D_004EAF58;                      // [C] 0x004EAF58: the thread id of the caller of sceCdInit (0x8 bytes)
extern unsigned char D_004EAF60[];          // [C] 0x004EAF60: its thread status (0x30), ReferThreadStatus' buffer
extern UnkStruct_ThreadParam D_004EAF90;    // [C] 0x004EAF90: the callback thread's CreateThread parameter
extern UnkStruct_CdSearchReq D_004EAFC0;    // [C] 0x004EAFC0: the search-file request (0x24 here, the name in D_004EAFE4's bytes: one block of 0x12C)
extern unsigned char D_004EB100[];          // [C] 0x004EB100: the search-file answer (0x40; +0 the result)
extern UnkStruct_SifClient D_004EB140;      // [C] 0x004EB140
extern UnkStruct_SifClient D_004EB168;      // [C] 0x004EB168
extern UnkStruct_SifClient D_004EB190;      // [C] 0x004EB190
extern unsigned int D_004EB1C0[];           // [C] 0x004EB1C0: sceCdInit's mode word (0x10)
extern unsigned int D_004EB1D0[];           // [C] 0x004EB1D0: the disk-ready mode word (0x10)
extern char D_004B3248[];                   // [C] 0x004B3248 .. 0x004B3438 messages (rodata)
extern char D_004B3270[];
extern char D_004B3290[];
extern char D_004B32A8[];
extern char D_004B32B8[];
extern char D_004B32C8[];
extern char D_004B32E0[];
extern char D_004B3308[];
extern char D_004B3320[];
extern char D_004B3330[];
extern char D_004B3340[];
extern char D_004B3368[];
extern char D_004B3380[];
extern char D_004B33A0[];
extern char D_004B33B0[];
extern char D_004B33C8[];
extern char D_004B33E8[];
extern char D_004B3400[];
extern char D_004B3418[];
extern char D_004B3438[];

extern "C" int scePrintf(const char *fmt, ...);
extern "C" void sceSifInitRpc(unsigned int mode);
extern "C" int sceSifBindRpc(UnkStruct_SifClient *cd, unsigned int id, unsigned int mode);
extern "C" int sceSifCallRpc(UnkStruct_SifClient *cd, unsigned int fno, unsigned int mode, void *send, int ssize,
                             void *recv, int rsize, unsigned int endfunc, unsigned int endpara);
extern "C" int sceSifCheckStatRpc(UnkStruct_SifClient *cd);
typedef void (*UnkFn_SifCmdHandler)(void);   // [C] _sceCd_Poff_Intr
extern "C" int sceSifAddCmdHandler(unsigned int cid, UnkFn_SifCmdHandler handler, int harg);
extern "C" int sceSifRemoveCmdHandler(unsigned int cid);
extern "C" int CreateSema(UnkStruct_SemaParam *param);
extern "C" int DeleteSema(int id);
extern "C" int WaitSema(int id);
extern "C" int PollSema(int id);
extern "C" int SignalSema(int id);
extern "C" int iSignalSema(int id);
extern "C" int GetThreadId(void);
extern "C" int ReferThreadStatus(int id, void *status);
extern "C" int CreateThread(UnkStruct_ThreadParam *param);
extern "C" int _StartThread(int id, void *arg);
extern "C" int ChangeThreadPriority(int id, int prio);
extern "C" void ExitDeleteThread(void);
extern "C" int DelayThread(unsigned int usec);
extern "C" int func_00114E60(void);         // [C] DIntr (returns whether interrupts were enabled)
extern "C" int EIntr(void);
extern "C" void func_0010E7D0(void *addr, int size);    // [C] libkernl 0x10E7D0: writes the data cache lines of [addr, addr + size) back

extern "C" int sceCdSync(int mode);
extern "C" int sceCdSyncS(int mode);
extern "C" void cmd_sem_init(void);
extern "C" int cdvd_exit(void);
extern "C" int sceCdLayerSearchFile(UnkStruct_CdFile *file, const char *name, int layer);
extern "C" int _sceCd_ncmd_prechk(int cmd);
extern "C" int _sceCd_scmd_prechk(int cmd);
extern "C" int sceCdDiskReady_old(int mode);
extern "C" int PowerOffCB(void);
extern "C" void func_0011E4A8(void);
extern "C" void func_0011E408(int *pnum);
extern "C" void _sceCd_Poff_Intr(void);

// [C] the busy delay after a bind that failed or returned no server yet: 0x100001 iterations (v0 = 0x100000, addiu -1 until -1).
static inline void cd_delay(void)
{
    int d;

    for (d = 0x100000; d != -1; d--) {
    }
}

// [C] sceCdCallback: sceCdSync(1) nonzero -> 0. Else (interrupts off) swaps D_004EAF40 with the new callback, D_004EAF44 = $gp;
// returns the old callback.
// 0x0011E3A0
extern "C" unsigned int func_0011E3A0(unsigned int func)
{
    unsigned int old;
    int en;

    if (sceCdSync(1) != 0) {
        return 0;
    }
    en = func_00114E60();
    old = D_004EAF40;
    D_004EAF40 = func;
    D_004EAF44 = CD_GP;
    if (en != 0) {
        EIntr();
    }
    return old;
}

// [C] the callback completion (called with a pointer to sceCdCbfunc_num by func_0011E648): sceCdCbfunc_num = *p, sceCdCbfunc_number =
// sceCdCbfunc_num; 0xB: sceCdCbfunc_num = 0, _sceCd_c_cb_sem = 0, done. Else iSignalSema(ncmd); with a callback thread and a user
// callback: iSignalSema(cb_semid); else _sceCd_c_cb_sem = 0. sceCdCbfunc_num = 0.
// 0x0011E408
extern "C" void func_0011E408(int *pnum)
{
    sceCdCbfunc_num = *pnum;
    sceCdCbfunc_number = sceCdCbfunc_num;
    if (sceCdCbfunc_num == 0xB) {
        sceCdCbfunc_num = 0;
        _sceCd_c_cb_sem = 0;
        return;
    }
    iSignalSema(_sceCd_ncmd_semid);
    if (cb_thid != 0 && D_004EAF40 != 0) {
        iSignalSema(cb_semid);
    } else {
        _sceCd_c_cb_sem = 0;
    }
    sceCdCbfunc_num = 0;
}

// [C] the callback thread: forever WaitSema(cb_semid); sceCdCbfunc_num == -1: _sceCd_c_cb_sem = 0, sceCdCbfunc_num = 0, cb_thid = 0,
// D_004EAF54 = 0, ExitDeleteThread. Debug trace; with D_004EAF40 and sceCdCbfunc_number nonzero: the user callback(number) with
// $gp = D_004EAF44 (restored after); _sceCd_c_cb_sem = 0.
// 0x0011E4A8
extern "C" void func_0011E4A8(void)
{
    typedef void (*UnkFn_CdCallback)(int);
    UnkFn_CdCallback cb;

    for (;;) {
        WaitSema(cb_semid);
        if (sceCdCbfunc_num == -1) {
            _sceCd_c_cb_sem = 0;
            sceCdCbfunc_num = 0;
            cb_thid = 0;
            D_004EAF54 = 0;
            ExitDeleteThread();
        }
        if (SCE_CD_debug > 0) {
            scePrintf(D_004B3248, D_004EAF40, sceCdCbfunc_number);
        }
        cb = (UnkFn_CdCallback)D_004EAF40;
        if (cb != 0 && sceCdCbfunc_number != 0) {
            // [C] s0 = $gp; $gp = D_004EAF44 around the call (no $gp in host C)
            cb(sceCdCbfunc_number);
        }
        _sceCd_c_cb_sem = 0;
    }
}

// [C] (inside the size of func_0011E4A8, no own label) the callback thread's start: no thread yet (cb_thid == 0): D_004EAF58 =
// GetThreadId(), ReferThreadStatus(it, D_004EAF60), the CreateThread parameter D_004EAF90 {+0xC = a2 stack size, +4 = func_0011E4A8,
// +8 = a1 stack, +0x14 = a0 priority, +0x10 = 0}, cb_thid = CreateThread, _StartThread(cb_thid, 0); returns 1. Else
// ChangeThreadPriority(cb_thid, a0); returns 0. [G] the three arguments' meaning from the ee_thread_t offsets.
// (not a function start of the asm's symbol table: at 0x0011E578, inside func_0011E4A8's size, so no marker)
extern "C" int func_0011E578(int prio, unsigned int stack, unsigned int stacksize)
{
    int tid;

    if (cb_thid == 0) {
        tid = GetThreadId();
        D_004EAF58 = tid;
        ReferThreadStatus(tid, D_004EAF60);
        D_004EAF90.field_C = stacksize;
        D_004EAF90.field_4 = (unsigned int)func_0011E4A8;
        D_004EAF90.field_8 = stack;
        D_004EAF90.field_14 = (unsigned int)prio;
        D_004EAF90.field_10 = 0;
        cb_thid = CreateThread(&D_004EAF90);
        _StartThread(cb_thid, 0);
        return 1;
    }
    ChangeThreadPriority(cb_thid, prio);
    return 0;
}

// [C] the SIF command handler of the completion packet: copies count1 bytes (re-read each pass) from +0x10 to dest1 (+8), count2
// bytes from +0x50 to dest2 (+0xC), each only while the count is positive (the asm stores each byte through the alias and the
// cached address: the same RAM), then func_0011E408(&sceCdCbfunc_num).
// 0x0011E648
extern "C" void func_0011E648(UnkStruct_CdCbPacket *pkt)
{
    int i;

    if (pkt->field_0 > 0) {
        for (i = 0; i < pkt->field_0; i++) {
            pkt->field_8[i] = pkt->field_10[i];
        }
    }
    if (pkt->field_4 > 0) {
        for (i = 0; i < pkt->field_4; i++) {
            pkt->field_C[i] = pkt->field_50[i];
        }
    }
    func_0011E408(&sceCdCbfunc_num);
}

// [C] all three command semaphores (+ cb_semid) exist: nothing. Else CreateSema {+4 = 1, +8 = 1, +0x14 = 0} into _sceCd_ncmd_semid,
// _sceCd_scmd_semid, _sceCd_rcmd_semid (each re-created when any is -1), then {+8 = 0} into cb_semid; _sceCd_c_cb_sem = 0.
// 0x0011E700
extern "C" void cmd_sem_init(void)
{
    UnkStruct_SemaParam p;

    if (_sceCd_ncmd_semid != -1) {
        if (_sceCd_scmd_semid != -1) {
            if (_sceCd_rcmd_semid != -1) {
                return;
            }
        }
    }
    p.field_0 = 0;      // [G] stack garbage in the asm
    p.field_C = 0;
    p.field_10 = 0;
    p.field_14 = 0;
    p.field_4 = 1;
    p.field_8 = 1;
    _sceCd_ncmd_semid = CreateSema(&p);
    _sceCd_scmd_semid = CreateSema(&p);
    _sceCd_rcmd_semid = CreateSema(&p);
    p.field_8 = 0;
    cb_semid = CreateSema(&p);
    _sceCd_c_cb_sem = 0;
}

// [C] with a callback thread: sceCdCbfunc_num = -1, SignalSema(cb_semid). The four semaphores deleted, then (interrupts off)
// sceSifRemoveCmdHandler(0x80000012); the tail call EIntr when they were on (its result returned), else the handler remover's.
// 0x0011E7B8
extern "C" int cdvd_exit(void)
{
    int en;
    int r;

    if (cb_thid != 0) {
        sceCdCbfunc_num = -1;
        SignalSema(cb_semid);
    }
    DeleteSema(_sceCd_ncmd_semid);
    DeleteSema(_sceCd_scmd_semid);
    DeleteSema(_sceCd_rcmd_semid);
    DeleteSema(cb_semid);
    en = func_00114E60();
    r = sceSifRemoveCmdHandler(0x80000012);
    if (en != 0) {
        return EIntr();
    }
    return r;
}

// [C] sceCdPOffCallback(func, arg): _icmd_bind < 0: PowerOffCB(). Interrupts off: swaps D_004EAF48 with func, D_004EAF50 = arg,
// D_004EAF4C = $gp; returns the old callback.
// 0x0011E858
extern "C" unsigned int sceCdPOffCallback(unsigned int func, unsigned int arg)
{
    unsigned int old;
    int en;

    if (_icmd_bind < 0) {
        PowerOffCB();
    }
    en = func_00114E60();
    old = D_004EAF48;
    D_004EAF50 = arg;
    D_004EAF48 = func;
    D_004EAF4C = CD_GP;
    if (en != 0) {
        EIntr();
    }
    return old;
}

// [C] the power-off command handler (cid 0x80000012): with a callback in D_004EAF48 and Init_seq == 0, calls it with D_004EAF50
// ($gp = D_004EAF4C around the call).
// 0x0011E8D8
extern "C" void _sceCd_Poff_Intr(void)
{
    typedef void (*UnkFn_CdPoff)(unsigned int);
    UnkFn_CdPoff cb;

    cb = (UnkFn_CdPoff)D_004EAF48;
    if (cb != 0) {
        if (Init_seq != 0) {
            return;
        }
        cb(D_004EAF50);
    }
}

// [C] Init_seq = 1; interrupts off, sceSifAddCmdHandler(0x80000012, _sceCd_Poff_Intr, 0), back on; Init_seq = 0; _icmd_bind = 1;
// returns 1.
// 0x0011E930
extern "C" int PowerOffCB(void)
{
    int en;

    Init_seq = 1;
    en = func_00114E60();
    sceSifAddCmdHandler(0x80000012, _sceCd_Poff_Intr, 0);
    if (en != 0) {
        EIntr();
    }
    Init_seq = 0;
    _icmd_bind = 1;
    return 1;
}

// [C] cmd_sem_init; PollSema(ncmd) != its id: 0. ncmd_sema_keep_cmd = 1, ReferThreadStatus(D_004EAF58, D_004EAF60); sceCdSync(1)
// nonzero: SignalSema, 0. sceSifInitRpc(0); _sf_bind < 0: bind D_004EB140 to 0x80000597 until it answers with a server (negative: trace
// D_004B3270; a zero server: no trace; both after the delay), _sf_bind = 0. The name (<= 0x100 bytes) copied to the request's +0x24
// (a full 0x100: last byte 0), trace D_004B3290, request +0x128 = layer, +0x124 = itself, the request written back (0x12C bytes),
// call fno 0 on D_004EB140 (send the request 0x12C, receive D_004EB100 4 bytes); negative: SignalSema, 0. Else the request's first 0x24
// bytes copied to *file [C] (the asm reads D_004EAFC0, not D_004EB100), traces (+8 / +4 / +0), then the answer's +0 is read,
// SignalSema, returned.
// 0x0011E9A8
extern "C" int sceCdLayerSearchFile(UnkStruct_CdFile *file, const char *name, int layer)
{
    unsigned char tmp[0x24];
    unsigned char c;
    int i;
    int r;

    cmd_sem_init();
    if (PollSema(_sceCd_ncmd_semid) != _sceCd_ncmd_semid) {
        return 0;
    }
    ncmd_sema_keep_cmd = 1;
    ReferThreadStatus(D_004EAF58, D_004EAF60);
    if (sceCdSync(1) != 0) {
        SignalSema(_sceCd_ncmd_semid);
        return 0;
    }
    sceSifInitRpc(0);
    if (_sf_bind < 0) {
        for (;;) {
            if (sceSifBindRpc(&D_004EB140, 0x80000597, 0) >= 0) {
                if (D_004EB140.field_24 != 0) {
                    break;
                }
            } else if (SCE_CD_debug > 0) {
                scePrintf(D_004B3270);
            }
            cd_delay();
        }
        _sf_bind = 0;
    }
    c = (unsigned char)name[0];
    D_004EAFC0.field_24[0] = c;
    i = 0;
    if (c != 0) {
        i = 1;
        while (i < 0x100) {
            c = (unsigned char)name[i];
            D_004EAFC0.field_24[i] = c;
            if (c == 0) {
                break;
            }
            i++;
        }
    }
    if (i == 0x100) {
        D_004EAFC0.field_24[0xFF] = 0;
    }
    if (SCE_CD_debug > 0) {
        scePrintf(D_004B3290, (unsigned int)D_004EAFC0.field_24);
    }
    D_004EAFC0.field_128 = layer;
    D_004EAFC0.field_124 = &D_004EAFC0;
    func_0010E7D0(&D_004EAFC0, 0x12C);
    if (sceSifCallRpc(&D_004EB140, 0, 0, &D_004EAFC0, 0x12C, D_004EB100, 4, 0, 0) < 0) {
        SignalSema(_sceCd_ncmd_semid);
        return 0;
    }
    // [C] the 0x24-byte copy (4 ldl/ldr + sdl/sdr doublewords, then one lwl/lwr + swl/swr word) from the alias of D_004EAFC0
    for (i = 0; i < 0x24; i++) {
        tmp[i] = D_004EAFC0.field_0[i];
    }
    for (i = 0; i < 0x24; i++) {
        ((unsigned char *)file)[i] = tmp[i];
    }
    if (SCE_CD_debug > 0) {
        scePrintf(D_004B32A8, (unsigned int)file->field_8);
        if (SCE_CD_debug > 0) {
            scePrintf(D_004B32B8, file->field_4);
            if (SCE_CD_debug > 0) {
                scePrintf(D_004B32C8, file->field_0);
            }
        }
    }
    r = (int)*(unsigned int *)D_004EB100;       // alias
    SignalSema(_sceCd_ncmd_semid);
    return r;
}

// [C] sceCdLayerSearchFile(file, name, 0).
// 0x0011ECC0
extern "C" int sceCdSearchFile(UnkStruct_CdFile *file, const char *name)
{
    return sceCdLayerSearchFile(file, name, 0);
}

// [C] cmd_sem_init; PollSema(ncmd) != its id: (debug > 0: trace D_004B32E0 with cmd and ncmd_sema_keep_cmd), 0. ncmd_sema_keep_cmd =
// cmd, ReferThreadStatus; sceCdSync(1) nonzero: SignalSema, 0. sceSifInitRpc(0); _ncmd_bind >= 0: 1. Else bind _sceCd_cd_ncmd to
// server 0x80000595 as the other clients (trace D_004B3308 on a negative result), _ncmd_bind = 0, 1.
// 0x0011ECE0
extern "C" int _sceCd_ncmd_prechk(int cmd)
{
    cmd_sem_init();
    if (PollSema(_sceCd_ncmd_semid) != _sceCd_ncmd_semid) {
        if (SCE_CD_debug > 0) {
            scePrintf(D_004B32E0, cmd, ncmd_sema_keep_cmd);
        }
        return 0;
    }
    ncmd_sema_keep_cmd = cmd;
    ReferThreadStatus(D_004EAF58, D_004EAF60);
    if (sceCdSync(1) != 0) {
        SignalSema(_sceCd_ncmd_semid);
        return 0;
    }
    sceSifInitRpc(0);
    if (_ncmd_bind >= 0) {
        return 1;
    }
    for (;;) {
        if (sceSifBindRpc(&_sceCd_cd_ncmd, 0x80000595, 0) >= 0) {
            if (_sceCd_cd_ncmd.field_24 != 0) {
                break;
            }
        } else if (SCE_CD_debug > 0) {
            scePrintf(D_004B3308);
        }
        cd_delay();
    }
    _ncmd_bind = 0;
    return 1;
}

// [C] _sceCd_ncmd_prechk(2) zero: 0. Call fno 0xE on _sceCd_cd_ncmd (no send, receive D_004858C0 4 bytes); negative: SignalSema, 0.
// Else the answer's +0 (alias) read, SignalSema, returned.
// 0x0011EE50
extern "C" int sceCdNcmdDiskReady(void)
{
    int r;

    if (_sceCd_ncmd_prechk(2) == 0) {
        return 0;
    }
    if (sceSifCallRpc(&_sceCd_cd_ncmd, 0xE, 0, 0, 0, D_004858C0, 4, 0, 0) < 0) {
        SignalSema(_sceCd_ncmd_semid);
        return 0;
    }
    r = (int)D_004858C0[0];     // alias
    SignalSema(_sceCd_ncmd_semid);
    return r;
}

// [C] mode 0: trace D_004B3320, then wait (DelayThread(0xFA0) per pass) while _sceCd_c_cb_sem is nonzero or the N-command client is
// busy (sceSifCheckStatRpc); returns 0. Mode != 0: 1 when a callback is pending or the client is busy, else 0.
// 0x0011EEE8
extern "C" int sceCdSync(int mode)
{
    if (mode == 0) {
        if (SCE_CD_debug > 0) {
            scePrintf(D_004B3320);
        }
        for (;;) {
            if (_sceCd_c_cb_sem != 0) {
                DelayThread(0xFA0);
                continue;
            }
            if (sceSifCheckStatRpc(&_sceCd_cd_ncmd) != 0) {
                DelayThread(0xFA0);
                continue;
            }
            return 0;
        }
    }
    if (_sceCd_c_cb_sem != 0) {
        return 1;
    }
    if (sceSifCheckStatRpc(&_sceCd_cd_ncmd) != 0) {
        return 1;
    }
    return 0;
}

// [C] mode 0: trace D_004B3330, wait (DelayThread(0xFA0)) while the S-command client is busy; returns 0. Else its sceSifCheckStatRpc.
// 0x0011EF88
extern "C" int sceCdSyncS(int mode)
{
    if (mode == 0) {
        if (SCE_CD_debug > 0) {
            scePrintf(D_004B3330);
        }
        while (sceSifCheckStatRpc(&_sceCd_cd_scmd) != 0) {
            DelayThread(0xFA0);
        }
        return 0;
    }
    return sceSifCheckStatRpc(&_sceCd_cd_scmd);
}

// [C] the S-command twin of _sceCd_ncmd_prechk: _sceCd_scmd_semid, scmd_sema_keep_cmd, sceCdSyncS(1), trace D_004B3340 (cmd, keep),
// _scmd_bind, _sceCd_cd_scmd bound to 0x80000593, trace D_004B3368.
// 0x0011EFF8
extern "C" int _sceCd_scmd_prechk(int cmd)
{
    cmd_sem_init();
    if (PollSema(_sceCd_scmd_semid) != _sceCd_scmd_semid) {
        if (SCE_CD_debug > 0) {
            scePrintf(D_004B3340, cmd, scmd_sema_keep_cmd);
        }
        return 0;
    }
    scmd_sema_keep_cmd = cmd;
    ReferThreadStatus(D_004EAF58, D_004EAF60);
    if (sceCdSyncS(1) != 0) {
        SignalSema(_sceCd_scmd_semid);
        return 0;
    }
    sceSifInitRpc(0);
    if (_scmd_bind >= 0) {
        return 1;
    }
    for (;;) {
        if (sceSifBindRpc(&_sceCd_cd_scmd, 0x80000593, 0) >= 0) {
            if (_sceCd_cd_scmd.field_24 != 0) {
                break;
            }
        } else if (SCE_CD_debug > 0) {
            scePrintf(D_004B3368);
        }
        cd_delay();
    }
    _scmd_bind = 0;
    return 1;
}

// [C] a table: cmd -> *out = ncmd semaphore id (0x8001), D_004858C0 (0x8003), D_00485940 (0x8002), _sceCd_cd_ncmd (0x8004), the
// S-command semaphore id (0x8011), _sceCd_scmdrdata (0x8013), _sceCd_scmdsdata (0x8012), _sceCd_cd_scmd (0x8014), 0 otherwise.
// Returns 1.
// 0x0011F168
extern "C" int func_0011F168(int cmd, unsigned int *out)
{
    unsigned int v;

    switch (cmd) {
    case 0x8001:
        v = (unsigned int)_sceCd_ncmd_semid;
        break;
    case 0x8003:
        v = (unsigned int)D_004858C0;
        break;
    case 0x8002:
        v = (unsigned int)D_00485940;
        break;
    case 0x8004:
        v = (unsigned int)&_sceCd_cd_ncmd;
        break;
    case 0x8011:
        *out = (unsigned int)_sceCd_scmd_semid;
        return 1;
    case 0x8013:
        v = (unsigned int)_sceCd_scmdrdata;
        break;
    case 0x8012:
        v = (unsigned int)_sceCd_scmdsdata;
        break;
    case 0x8014:
        v = (unsigned int)&_sceCd_cd_scmd;
        break;
    default:
        v = 0;
        break;
    }
    *out = v;
    return 1;
}

// [C] sceCdInit(mode): sceCdSyncS(1) nonzero: 0. sceSifInitRpc(0); Init_seq = 1, D_004EAF58 = GetThreadId(), the binds (_icmd_bind,
// _sf_bind, _ncmd_bind, _scmd_bind, _scmd_dr_bind) = -1, _sceCd_ee_read_mode = 0, c_cnt + 1, _it_bind = -1. Bind D_004EB168 to
// server 0x80000592 until it has a server (a negative result: trace D_004B3380 with the result and the count; both after the delay).
// D_004EB1C0 = mode, _it_bind = 0, the word written back, call fno 0 (send D_004EB1C0 4 bytes, receive _sceCd_scmdrdata 0x10);
// negative: Init_seq = 0, 0. The answer's +0xC / +4 / +8 (alias): +0xC == 0xFF: result 1; 0xFE: SCE_CD_debug = 1, result 1; else
// the result is 2 when +4 / 256 < 2 or +8 / 256 < 2 (truncating division), else 1. Init_seq = 0. mode == 5: trace D_004B33A0,
// cdvd_exit, the three semaphore ids = -1; else cmd_sem_init, PowerOffCB. Returns the result.
// 0x0011F218
extern "C" int sceCdInit(int mode)
{
    int tid;
    int c;
    int r;
    int res;
    int a1;
    int a0;
    int a2;
    int t;

    if (sceCdSyncS(1) != 0) {
        return 0;
    }
    sceSifInitRpc(0);
    tid = GetThreadId();
    Init_seq = 1;
    c = D_00485894 + 1;
    D_004EAF58 = tid;
    _icmd_bind = -1;
    _sf_bind = -1;
    _ncmd_bind = -1;
    _scmd_bind = -1;
    _scmd_dr_bind = -1;
    _sceCd_ee_read_mode = 0;
    D_00485894 = c;
    _it_bind = -1;
    for (;;) {
        r = sceSifBindRpc(&D_004EB168, 0x80000592, 0);
        if (r >= 0) {
            if (D_004EB168.field_24 != 0) {
                break;
            }
        } else if (SCE_CD_debug > 0) {
            scePrintf(D_004B3380, r, D_00485894);
        }
        cd_delay();
    }
    D_004EB1C0[0] = (unsigned int)mode;
    _it_bind = 0;
    func_0010E7D0(D_004EB1C0, 4);
    if (sceSifCallRpc(&D_004EB168, 0, 0, D_004EB1C0, 4, _sceCd_scmdrdata, 0x10, 0, 0) < 0) {
        Init_seq = 0;
        return 0;
    }
    a2 = (int)_sceCd_scmdrdata[3];  // alias
    a1 = (int)_sceCd_scmdrdata[1];
    a0 = (int)_sceCd_scmdrdata[2];
    res = 1;
    if (a2 == 0xFF) {
        // result 1
    } else if (a2 == 0xFE) {
        SCE_CD_debug = 1;
    } else {
        t = (-1 < a1) ? a1 : a1 + 0xFF;
        if ((t >> 8) < 2) {
            res = 2;
        } else {
            t = (-1 < a0) ? a0 : a0 + 0xFF;
            if ((t >> 8) < 2) {
                res = 2;
            }
        }
    }
    Init_seq = 0;
    if (mode < 0 || mode < 2 || mode != 5) {
        cmd_sem_init();
        PowerOffCB();
        return res;
    }
    if (SCE_CD_debug > 0) {
        scePrintf(D_004B33A0);
    }
    cdvd_exit();
    _sceCd_ncmd_semid = -1;
    _sceCd_scmd_semid = -1;
    cb_semid = -1;
    return res;
}

// [C] the older sceCdDiskReady: trace D_004B33B0, cmd_sem_init, PollSema(scmd) != its id: 6. sceCdSyncS(1) nonzero: (below). sceSifInitRpc(0);
// _scmd_dr_bind < 0: bind D_004EB190 to 0x8000059A until it has a server (trace D_004B33C8 on a negative result), _scmd_dr_bind =
// 0. D_004EB1D0 = mode, written back, call fno 0 (send D_004EB1D0 4 bytes, receive _sceCd_scmdrdata 4) on D_004EB190; negative or the
// sync case: SignalSema(scmd), -1 for mode 8 else 6. Else trace D_004B33E8, the answer's +0 (alias), SignalSema, returned.
// 0x0011F4F8
extern "C" int sceCdDiskReady_old(int mode)
{
    int r;

    if (SCE_CD_debug > 0) {
        scePrintf(D_004B33B0);
    }
    cmd_sem_init();
    if (PollSema(_sceCd_scmd_semid) != _sceCd_scmd_semid) {
        return 6;
    }
    if (sceCdSyncS(1) != 0) {
        goto fail;
    }
    sceSifInitRpc(0);
    if (_scmd_dr_bind < 0) {
        for (;;) {
            if (sceSifBindRpc(&D_004EB190, 0x8000059A, 0) >= 0) {
                if (D_004EB190.field_24 != 0) {
                    break;
                }
            } else if (SCE_CD_debug > 0) {
                scePrintf(D_004B33C8);
            }
            cd_delay();
        }
        _scmd_dr_bind = 0;
    }
    D_004EB1D0[0] = (unsigned int)mode;
    func_0010E7D0(D_004EB1D0, 4);
    if (sceSifCallRpc(&D_004EB190, 0, 0, D_004EB1D0, 4, _sceCd_scmdrdata, 4, 0, 0) < 0) {
        goto fail;
    }
    if (SCE_CD_debug > 0) {
        scePrintf(D_004B33E8);
    }
    r = (int)_sceCd_scmdrdata[0];       // alias
    SignalSema(_sceCd_scmd_semid);
    return r;
fail:
    SignalSema(_sceCd_scmd_semid);
    return (mode == 8) ? -1 : 6;
}

// [C] sceCdDiskReady(mode): trace D_004B3400, cmd_sem_init, PollSema(rcmd) != its id: -1 for mode 8 else 6. sceSifInitRpc(0);
// _scmd_dr_bind < 0: bind D_004EB190 to 0x8000059C (a negative result: trace D_004B33C8, retry after the delay; a zero server:
// after the delay, counted: at the 17th such answer (s1 >= 0x11 on the next one) SignalSema(rcmd) and return sceCdDiskReady_old(mode)),
// _scmd_dr_bind = 0. D_004EB1D0 = mode, written back, call fno 0 (send D_004EB1D0 4 bytes, receive _sceCd_rcmdrdata 4) on D_004EB190;
// negative: SignalSema(rcmd), -1 for mode 8 else 6. Else trace D_004B33E8, the answer's +0 (alias), SignalSema, returned.
// 0x0011F6F0
extern "C" int sceCdDiskReady(int mode)
{
    int n;
    int r;

    if (SCE_CD_debug > 0) {
        scePrintf(D_004B3400);
    }
    cmd_sem_init();
    if (PollSema(_sceCd_rcmd_semid) != _sceCd_rcmd_semid) {
        return (mode == 8) ? -1 : 6;
    }
    sceSifInitRpc(0);
    n = 0;
    if (_scmd_dr_bind < 0) {
        for (;;) {
            if (sceSifBindRpc(&D_004EB190, 0x8000059C, 0) >= 0) {
                if (D_004EB190.field_24 != 0) {
                    break;
                }
                if (n >= 0x11) {
                    SignalSema(_sceCd_rcmd_semid);
                    return sceCdDiskReady_old(mode);
                }
                cd_delay();
                n = n + 1;
            } else {
                if (SCE_CD_debug > 0) {
                    scePrintf(D_004B33C8);
                }
                cd_delay();
            }
        }
        _scmd_dr_bind = 0;
    }
    D_004EB1D0[0] = (unsigned int)mode;
    func_0010E7D0(D_004EB1D0, 4);
    if (sceSifCallRpc(&D_004EB190, 0, 0, D_004EB1D0, 4, _sceCd_rcmdrdata, 4, 0, 0) < 0) {
        SignalSema(_sceCd_rcmd_semid);
        return (mode == 8) ? -1 : 6;
    }
    if (SCE_CD_debug > 0) {
        scePrintf(D_004B33E8);
    }
    r = (int)_sceCd_rcmdrdata[0];       // alias
    SignalSema(_sceCd_rcmd_semid);
    return r;
}

// [C] _sceCd_scmd_prechk(1) zero: 0. Call fno 3 on _sceCd_cd_scmd (no send, receive _sceCd_scmdrdata 4 bytes); negative: SignalSema, 0.
// Else the answer's +0 (alias), SignalSema, returned.
// 0x0011F8F8
extern "C" int sceCdGetDiskType(void)
{
    int r;

    if (_sceCd_scmd_prechk(1) == 0) {
        return 0;
    }
    if (sceSifCallRpc(&_sceCd_cd_scmd, 3, 0, 0, 0, _sceCd_scmdrdata, 4, 0, 0) < 0) {
        SignalSema(_sceCd_scmd_semid);
        return 0;
    }
    r = (int)_sceCd_scmdrdata[0];       // alias
    SignalSema(_sceCd_scmd_semid);
    return r;
}

// [C] _sceCd_scmd_prechk(0x21) zero: 0. Call fno 0x21 on _sceCd_cd_scmd (no send, receive _sceCd_scmdrdata 8 bytes); negative:
// SignalSema, 0. Else *out = the answer's +4, SignalSema, the answer's +0 (alias, read before the call) returned.
// 0x0011F990
extern "C" int func_0011F990(int *out)
{
    int r;

    if (_sceCd_scmd_prechk(0x21) == 0) {
        return 0;
    }
    if (sceSifCallRpc(&_sceCd_cd_scmd, 0x21, 0, 0, 0, _sceCd_scmdrdata, 8, 0, 0) < 0) {
        SignalSema(_sceCd_scmd_semid);
        return 0;
    }
    *out = (int)_sceCd_scmdrdata[1];    // alias
    r = (int)_sceCd_scmdrdata[0];
    SignalSema(_sceCd_scmd_semid);
    return r;
}

// [C] _sceCd_scmd_prechk(0x22) zero: 0. _sceCd_scmdsdata[0] = mode, written back (4 bytes), call fno rpc_mmode on _sceCd_cd_scmd (send
// _sceCd_scmdsdata outcnt_mmode bytes, receive _sceCd_scmdrdata incnt_mmode bytes); negative: SignalSema, 0. Else the answer's +0
// (alias), SignalSema, returned.
// 0x0011FA48
extern "C" int sceCdMmode(int mode)
{
    int r;

    if (_sceCd_scmd_prechk(0x22) == 0) {
        return 0;
    }
    _sceCd_scmdsdata[0] = (unsigned int)mode;
    func_0010E7D0(_sceCd_scmdsdata, 4);
    if (sceSifCallRpc(&_sceCd_cd_scmd, (unsigned int)rpc_mmode, 0, _sceCd_scmdsdata, outcnt_mmode, _sceCd_scmdrdata,
                      incnt_mmode, 0, 0) < 0) {
        SignalSema(_sceCd_scmd_semid);
        return 0;
    }
    r = (int)_sceCd_scmdrdata[0];       // alias
    SignalSema(_sceCd_scmd_semid);
    return r;
}

// [C] _sceCd_scmd_prechk(0xF) zero: 0. Trace D_004B3418, call fno 1 on _sceCd_cd_scmd (no send, receive _sceCd_scmdrdata 0x10 bytes);
// negative: SignalSema, 0. Else the answer's bytes +4..+0xB (alias, ldl/ldr; sdl/sdr) copied to clock, trace D_004B3438, the answer's
// +0 read, SignalSema, returned.
// 0x0011FB18
extern "C" int sceCdReadClock(unsigned char *clock)
{
    unsigned char *rd;
    int r;
    int i;

    if (_sceCd_scmd_prechk(0xF) == 0) {
        return 0;
    }
    if (SCE_CD_debug > 0) {
        scePrintf(D_004B3418);
    }
    if (sceSifCallRpc(&_sceCd_cd_scmd, 1, 0, 0, 0, _sceCd_scmdrdata, 0x10, 0, 0) < 0) {
        SignalSema(_sceCd_scmd_semid);
        return 0;
    }
    rd = (unsigned char *)_sceCd_scmdrdata + 4;     // alias
    {
        unsigned char tmp[8];
        for (i = 0; i < 8; i++) {
            tmp[i] = rd[i];
        }
        for (i = 0; i < 8; i++) {
            clock[i] = tmp[i];
        }
    }
    if (SCE_CD_debug > 0) {
        scePrintf(D_004B3438);
    }
    r = (int)_sceCd_scmdrdata[0];
    SignalSema(_sceCd_scmd_semid);
    return r;
}

// [C] _sceCd_scmd_prechk(0x1A) zero: -1. _sceCd_scmdsdata[0] = arg, written back (4 bytes), call fno 5 on _sceCd_cd_scmd (send
// _sceCd_scmdsdata 4 bytes, receive _sceCd_scmdrdata 8 bytes); negative: SignalSema, -1. Else (out != 0) *out = the answer's +4;
// the answer's +0 (alias, read before the call) returned after SignalSema.
// 0x0011FC10
extern "C" int func_0011FC10(int arg, int *out)
{
    int r;

    if (_sceCd_scmd_prechk(0x1A) == 0) {
        return -1;
    }
    _sceCd_scmdsdata[0] = (unsigned int)arg;
    func_0010E7D0(_sceCd_scmdsdata, 4);
    if (sceSifCallRpc(&_sceCd_cd_scmd, 5, 0, _sceCd_scmdsdata, 4, _sceCd_scmdrdata, 8, 0, 0) < 0) {
        SignalSema(_sceCd_scmd_semid);
        return -1;
    }
    if (out != 0) {
        *out = (int)_sceCd_scmdrdata[1];        // alias
    }
    r = (int)_sceCd_scmdrdata[0];
    SignalSema(_sceCd_scmd_semid);
    return r;
}
