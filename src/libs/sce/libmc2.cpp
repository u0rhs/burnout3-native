// Sony EE library libmc2 (SDK 2.8.0 as retail links it: .data id string "PsIIlibmc2  2800" at 0x0049B6D8): the EE side
// of the memory card library over libdbc sockets (sceMc2*). Written from the retail asm (decomp
// assembly/asm/sce/libmc2.s, data asm/data/sce/libmc2.{data,bss,rodata}.s). Tags: [C] confirmed from the asm,
// [L] likely, [G] guess.
// Part 1 of 3 (the first 43 functions of the file, in file order): sceMc2Init, sceMc2End, sceMc2CreateSocket,
// sceMc2DeleteSocket, sceMc2SearchSocket, sceMc2ChangeThreadPriority, sceMc2iChangeThreadPriority, func_00241F98,
// func_00242018, sceMc2GetInfoAsync, func_00242128, func_002421E8, func_00242268, func_00242348, func_00242428,
// func_002424E0, sceMc2GetDirAsync, func_00242688, func_00242740, func_00242800, func_002428C0, func_002429A8,
// basic_thread, sceMc2Sync2, func_00242E00, sceMc2Sync, sceMc2GetInfo, sceMc2Format, sceMc2Unformat, sceMc2ReadFile,
// func_00242F50, func_00242F90, func_00242FD0, func_00243010, func_00243050, func_00243090, func_002430D0,
// sceMc2Chmod, sceMc2SearchFile, sceMc2GetEntSpace, func_002431D0, isCorrNameSize, isCorrFname.
// Part 2 of 3 (functions 44 to 86, boot2 2026-11-06): func_00243300, isWildFit, _strtok, _get_dir_nest, func_002436C0,
// _sceMcFatInit, func_002438B8, _sceMcFatFlushFatCache, _sceMcFatReadFatData, _sceMcFatGetNextClust,
// _sceMcFatWriteFatData, _sceMcFatGetFreeClust, _sceMcFatGetFreeClustSize, _GetEntryByPos, func_00244118,
// _reset_problem_patch, _sceMcFatGetInfo, _sceMcFatFormat, _sceMcFatUnFormat, func_00245040, _SearchEntry,
// _sceMcFatSearchFile, _SetEntryByPos, _GetFreeEntrySize, _CreateEntry, func_00245FC0, func_00246588, func_00246788,
// func_00246DC8, func_00246FC8, _sceMcFatGetDir, func_00247580, _sceMcFatRename, func_00247808, _sceMcpInit,
// func_002478D8, func_002478F0, func_00247908, _sceMcpReadSysInfo, _sceMcpWriteSysInfo, func_00247B70, func_00247BB8,
// func_00247C00 (part 2 follows part 1 in this file).
// Part 3 of 3 (functions 87 to 127, boot2 2026-11-06): _sceMcpWriteUserClustOnCache, func_00248040, func_00248188,
// func_002481A0, func_002481B8, func_00248210, func_00248268, SyncDma, SyncDmaForRead, sceMc2SifCallback, _sceMcCoreInit,
// _sceMcCoreEnd, func_00248640, _sceMcCoreCreateSocket, func_00248750, _sceMcCoreDeleteSocket, func_002487E0,
// func_00248828, _sceMcCoreGetCardSpec, func_00248A68, _sceMcCoreCheckNewCard, _sceMcCoreWriteBack, _sceMcCoreReadClock,
// _sceMcCoreReadPage2, func_00249160, _sceMcCoreWritePage, _sceMcCoreWriteSameData, _sceMcCoreBlockErase, func_00249638,
// func_00249640, func_00249680, func_002496A0, func_002496C0, initialize_mc2_socket, create_mc2_socket,
// delete_mc2_socket, my_open, func_002498F8, func_00249928, sceNetcnfifCreateMc2Thread, sceNetcnfifDeleteMc2Thread.
// [C] Design: one request slot D_01D810C0 (command word +4 nonzero = busy) is filled by an *Async function and a
// worker thread (basic_thread, a command switch) executes it and signals the semaphore D_01D815E8, which sceMc2Sync2
// waits for / polls. Error codes are 0x8101xxxx / 0x8101 8xxx constants as the asm loads them.
// boot2 (2026-11-06).

// [C] the request slot D_01D810C0 (0x520 bytes: D_01D810C0 + D_01D810C4 in the symbol table, one object).
struct UnkStruct_McRequest {
    int field_0;                // [C] the socket (a0 of the *Async function)
    int field_4;                // [C] the command (0 = idle / done); the worker's switch index is field_4 - 2
    int field_8;                // [C] the result the worker stores
    unsigned int field_C;       // [C] command argument 0
    unsigned int field_10;      // [C] command argument 1
    unsigned int field_14;      // [C] command argument 2
    unsigned int field_18;      // [C] command argument 3
    unsigned char pad_1C[0x400];
    char field_41C[0x80];       // [C] the path / name (strlen < 0x80 checked, strcpy'd)
    char field_49C[0x80];       // [C] the second name (rename)
    unsigned char pad_51C[4];
};

// [C] the thread / semaphore ids: +0 thread, +4 semaphore (created first, deleted first), +8 semaphore the worker
// signals (D_01D815E8 is the same word).
struct UnkStruct_McIds {
    int field_0;
    int field_4;
    int field_8;
};

// [C] the semaphore parameter on sceMc2Init's stack ([L] SemaParam: count, max, init, ...): the asm sets max = 1,
// init = 0 only.
struct UnkStruct_SemaParam {
    int field_0;
    int field_4;
    int field_8;
    int field_C;
    unsigned int field_10;
    unsigned int field_14;
};

// [C] the thread parameter on sceMc2Init's stack ([L] ThreadParam: attr, entry, stack, stackSize, gpReg,
// initPriority, ...): the asm sets entry..initPriority only.
struct UnkStruct_ThreadParam {
    unsigned int field_0;
    void (*field_4)(void);
    void *field_8;
    unsigned int field_C;
    void *field_10;
    unsigned int field_14;
    unsigned int field_18;
};

// [C] sceMc2CreateSocket's request for _sceMcCoreCreateSocket (0x18 bytes used)
struct UnkStruct_McSocket {
    unsigned int field_0;
    unsigned int field_4;
    unsigned int field_8;
    unsigned int field_C;
    unsigned int field_10;
    unsigned char field_14;
    unsigned char pad_15[3];
};

// [C] the directory entry sceMc2 search-file builds on the worker's stack (_sceMcFatSearchFile's a2): +0 attr (u16),
// +4 size, +8 / +0x18 8-byte time stamps, +0x40 name.
struct UnkStruct_McDirEnt {
    unsigned short field_0;
    unsigned char pad_2[2];
    unsigned int field_4;
    unsigned char field_8[8];
    unsigned char pad_10[8];
    unsigned char field_18[8];
    unsigned char pad_20[0x20];
    char field_40[0x1C0];
};

extern int D_0049B6E8;                      // [C] 0x0049B6E8 (.data): 1 after sceMc2Init
extern UnkStruct_McRequest D_01D810C0;      // [C] 0x01D810C0 (.bss): the request slot
extern int D_01D810C4;                      // [C] 0x01D810C4: its command word (same as D_01D810C0.field_4)
extern UnkStruct_McIds D_01D815E0;          // [C] 0x01D815E0: thread / semaphore ids
extern int D_01D815E8;                      // [C] 0x01D815E8: the worker's done semaphore (D_01D815E0.field_8)
extern char D_01D81600[];                   // [C] 0x01D81600 (0xC000): the worker thread's stack
extern char D_004E8670[];                   // [C] 0x004E8670: the gp value passed to CreateThread ([L] _gp of the library's object)
extern char D_004BB300[];                   // [C] 0x004BB300 (rodata): "libmc2: TerminateTread faild\n"

extern "C" int CreateSema(UnkStruct_SemaParam *param);
extern "C" int DeleteSema(int id);
extern "C" int WaitSema(int id);
extern "C" int PollSema(int id);
extern "C" int CreateThread(UnkStruct_ThreadParam *param);
extern "C" int _StartThread(int id, void *arg);
extern "C" int TerminateThread(int id);
extern "C" int DeleteThread(int id);
extern "C" int ChangeThreadPriority(int id, int prio);
extern "C" int iChangeThreadPriority(int id, int prio);
extern "C" int SignalSema(int id);
extern "C" int scePrintf(const char *fmt, ...);
extern "C" unsigned int strlen(const char *s);
extern "C" char *strcpy(char *dst, const char *src);
extern "C" char *strchr(const char *s, int c);
extern "C" char *strrchr(const char *s, int c);
extern "C" int sceDbcSearchPortSpecific(unsigned int *param);

// libmc2's own functions outside part 1 (parts 2 / 3 define them)
extern "C" int _sceMcCoreInit(void);
extern "C" int _sceMcpInit(void);
extern "C" int _sceMcFatInit(void);
extern "C" int _sceMcCoreEnd(void);
extern "C" int _sceMcCoreCreateSocket(UnkStruct_McSocket *s);
extern "C" int _sceMcCoreDeleteSocket(int socket);
extern "C" int _sceMcCoreReadPage2(int a0, unsigned int a1, unsigned int a2, int a3, unsigned int a4);
extern "C" int _sceMcCoreWritePage(int a0, unsigned int a1, unsigned int a2, unsigned int a3);
extern "C" int _sceMcFatGetInfo(int socket, unsigned int a1);
extern "C" int _sceMcFatFormat(int socket);
extern "C" int _sceMcFatUnFormat(int socket);
extern "C" int _sceMcFatGetDir(int socket, char *name, unsigned int a2, unsigned int a3, unsigned int a4,
                               unsigned int a5);
extern "C" int _sceMcFatRename(int socket, char *name, char *name2);
extern "C" int _sceMcFatSearchFile(int socket, char *name, UnkStruct_McDirEnt *ent, int *a3, int *a4);
extern "C" int func_00245FC0(int socket, char *name, unsigned int a2);
extern "C" int func_00246588(int socket, char *name, unsigned int a2, unsigned int a3, unsigned int a4);
extern "C" int func_00246788(int socket, char *name, unsigned int a2, unsigned int a3, unsigned int a4);
extern "C" int func_00246DC8(int socket, char *name);
extern "C" int func_00246FC8(int socket, char *name, unsigned int a2);
extern "C" int func_00247580(int socket, char *name, unsigned int a2);
extern "C" int func_00247808(int socket, char *name);
extern "C" int func_00248750(int socket);    // [C] 1 when the socket (0 or 1) is created (D_01D93488 + socket * 0x1C)
extern "C" int func_002487E0(int a0);
extern "C" int func_00249680(void);          // [C] WaitSema(D_01D93484) (tail call)
extern "C" int func_002496A0(void);          // [C] SignalSema(D_01D93484) (tail call)

extern "C" int sceMc2End(void);
extern "C" void basic_thread(void);
extern "C" int sceMc2Sync2(int nowait, int *a1, int *a2);
extern "C" int sceMc2GetInfoAsync(int socket, unsigned int a1);
extern "C" int func_00241F98(int socket);
extern "C" int func_00242018(int socket);
extern "C" int func_00242128(int socket, const char *name, unsigned int a2);
extern "C" int func_00242268(int socket, const char *name, unsigned int a2, unsigned int a3, unsigned int a4);
extern "C" int func_00242348(int socket, const char *name, unsigned int a2, unsigned int a3, unsigned int a4);
extern "C" int func_00242428(int socket, const char *name);
extern "C" int func_002424E0(int socket, const char *name);
extern "C" int sceMc2GetDirAsync(int socket, const char *name, unsigned int a2, unsigned int a3, unsigned int a4,
                                 unsigned int a5);
extern "C" int func_00242688(int socket, const char *name);
extern "C" int func_00242740(int socket, const char *name, unsigned int a2);
extern "C" int func_00242800(int socket, const char *name, unsigned int a2);
extern "C" int func_002428C0(int socket, const char *name, const char *name2);
extern "C" int func_002429A8(int socket, const char *name);

// [C] the common head of every *Async function: not initialised -> 0x81018001; socket not created (func_00248750) ->
// error 0x81018002; request slot busy (+4 nonzero) -> 0x81010010; else 0 (go on).
static inline int mc2_begin(int socket)
{
    if (D_0049B6E8 == 0) {
        return 0x81018001;
    }
    if (func_00248750(socket) == 0) {
        return 0x81018002;
    }
    if (D_01D810C0.field_4 != 0) {
        return 0x81010010;
    }
    return 0;
}

// [C] the synchronous wrappers' tail: the async result stored on the stack, and when it is 0 sceMc2Sync2(0, 0, &r)
// overwrites it with the worker's result.
static inline int mc2_wait(int r)
{
    int res = r;

    if (res == 0) {
        sceMc2Sync2(0, 0, &res);
    }
    return res;
}

// [C] already initialised: 0. A nonzero argument: 0x81010016. Else state = 1; ids = -1 (+8, +0, +4); CreateSema {max 1,
// init 0} -> +4 (< 0: 0x81018003); CreateSema again -> +8 (< 0: sceMc2End, 0x81018003); D_01D810C4 = 0; CreateThread
// {basic_thread, stack D_01D81600, 0xC000, gp D_004E8670, priority 1} -> +0 (< 0: sceMc2End, 0x81018004); the three
// core inits; _StartThread(+0, 0); 0.
// 0x00241BC0
extern "C" int sceMc2Init(int a0)
{
    // boot2 E1: locals passed down to calls live on the guest stack at retail's offsets
    // [C 0x241BC0 addiu sp, -0xA0; addiu s0, sp, 0x60 (CreateSema a0); daddu a0, sp (CreateThread a0)]
    B3_RETAIL_FRAME(0xA0);
    UnkStruct_SemaParam &sp = *B3_FRAME_LOCAL(UnkStruct_SemaParam, 0x60);   // sp+0x60
    UnkStruct_ThreadParam &tp = *B3_FRAME_LOCAL(UnkStruct_ThreadParam, 0);   // sp+0
    int id;

    if (D_0049B6E8 == 1) {
        return 0;
    }
    if (a0 != 0) {
        return 0x81010016;
    }
    D_0049B6E8 = 1;
    D_01D815E0.field_8 = -1;
    D_01D815E0.field_0 = -1;
    D_01D815E0.field_4 = -1;
    sp.field_0 = 0; // [G] not written by the asm (the stack's value; host: 0)
    sp.field_C = 0;
    sp.field_10 = 0;
    sp.field_14 = 0;
    sp.field_8 = 0;
    sp.field_4 = 1;
    id = CreateSema(&sp);
    D_01D815E0.field_4 = id;
    if (id < 0) {
        return 0x81018003;
    }
    sp.field_8 = 0;
    sp.field_4 = 1;
    id = CreateSema(&sp);
    D_01D815E0.field_8 = id;
    if (id < 0) {
        sceMc2End();
        return 0x81018003;
    }
    D_01D810C4 = 0;
    tp.field_0 = 0; // [G] not written by the asm (host: 0)
    tp.field_18 = 0;
    tp.field_4 = basic_thread;
    tp.field_8 = D_01D81600;
    tp.field_C = 0xC000;
    tp.field_10 = D_004E8670;
    tp.field_14 = 1;
    id = CreateThread(&tp);
    D_01D815E0.field_0 = id;
    if (id < 0) {
        sceMc2End();
        return 0x81018004;
    }
    _sceMcCoreInit();
    _sceMcpInit();
    _sceMcFatInit();
    _StartThread(D_01D815E0.field_0, 0);
    return 0;
}

// [C] not initialised: 0x81018001. Request busy (D_01D810C4): 0x81010010. Else the semaphores deleted when >= 0
// (+4, +8), the thread (+0, when >= 0) terminated (< 0: the message "libmc2: TerminateTread faild") and deleted,
// _sceMcCoreEnd, state = 0, 0.
// 0x00241D08
extern "C" int sceMc2End(void)
{
    if (D_0049B6E8 == 0) {
        return 0x81018001;
    }
    if (D_01D810C4 != 0) {
        return 0x81010010;
    }
    if (D_01D815E0.field_4 >= 0) {
        DeleteSema(D_01D815E0.field_4);
    }
    if (D_01D815E0.field_8 >= 0) {
        DeleteSema(D_01D815E0.field_8);
    }
    if (D_01D815E0.field_0 >= 0) {
        if (TerminateThread(D_01D815E0.field_0) < 0) {
            scePrintf(D_004BB300);
        }
        DeleteThread(D_01D815E0.field_0);
    }
    _sceMcCoreEnd();
    D_0049B6E8 = 0;
    return 0;
}

// [C] not initialised: 0x81018001. The parameter's +4 not 2 or 3 or +8 nonzero: 0x81010016. Else the core request
// {+0 = param[0] | 1, +4 = 2, +8 = param[1] word (+4), +0xC = 0, +0x10 = param[3], byte +0x14 = 0}; a result < 0:
// error 0x81010017, else the result.
// 0x00241DC8
extern "C" int sceMc2CreateSocket(unsigned int *param)
{
    // boot2 E1: locals passed down to calls live on the guest stack at retail's offsets
    // [C 0x241DCC addiu sp, -0x40; daddu a0, sp (_sceMcCoreCreateSocket a0)]
    B3_RETAIL_FRAME(0x40);
    UnkStruct_McSocket &s = *B3_FRAME_LOCAL(UnkStruct_McSocket, 0);   // sp+0
    int r;

    if (D_0049B6E8 == 0) {
        return 0x81018001;
    }
    if (param[1] - 2 >= 2) {
        return 0x81010016;
    }
    if (param[2] != 0) {
        return 0x81010016;
    }
    s.field_8 = param[1];
    s.field_10 = param[3];
    s.field_C = 0;
    s.field_0 = param[0] | 1;
    s.field_4 = 2;
    s.field_14 = 0;
    r = _sceMcCoreCreateSocket(&s);
    if (r < 0) {
        return 0x81010017;
    }
    return r;
}

// [C] not initialised: 0x81018001; busy: 0x81010010. _sceMcCoreDeleteSocket(socket) (the asm leaves a0 untouched):
// nonzero -> 0, zero -> 0x81018002.
// 0x00241E60
extern "C" int sceMc2DeleteSocket(int socket)
{
    int r;

    if (D_0049B6E8 == 0) {
        return 0x81018001;
    }
    if (D_01D810C4 != 0) {
        return 0x81010010;
    }
    r = _sceMcCoreDeleteSocket(socket);
    if (r != 0) {
        return 0;
    }
    return 0x81018002;
}

// [C] not initialised: 0x81018001; busy: 0x81010010. sceDbcSearchPortSpecific on {+8 = a0, +0xC = a1}; < 0:
// error 0x81018002; else func_002487E0(result).
// 0x00241EB8
extern "C" int sceMc2SearchSocket(unsigned int a0, unsigned int a1)
{
    // boot2 E1: locals passed down to calls live on the guest stack at retail's offsets
    // [C 0x241EBC addiu sp, -0x40; daddu a0, sp; sw a3, 0x8(sp); sw a1, 0xC(sp)]
    B3_RETAIL_FRAME(0x40);
    unsigned int *s = B3_FRAME_LOCAL(unsigned int, 0);   // sp+0, 4 words
    int r;

    if (D_0049B6E8 == 0) {
        return 0x81018001;
    }
    if (D_01D810C4 != 0) {
        return 0x81010010;
    }
    s[0] = 0; // [G] not written by the asm (host: 0)
    s[1] = 0;
    s[2] = a0;
    s[3] = a1;
    r = sceDbcSearchPortSpecific(s);
    if (r < 0) {
        return 0x81018002;
    }
    return func_002487E0(r);
}

// [C] priority <= 0: 0x81010016. Else ChangeThreadPriority(the worker thread, priority); 0.
// 0x00241F28
extern "C" int sceMc2ChangeThreadPriority(int prio)
{
    if (prio <= 0) {
        return 0x81010016;
    }
    ChangeThreadPriority(D_01D815E0.field_0, prio);
    return 0;
}

// [C] as sceMc2ChangeThreadPriority with iChangeThreadPriority (interrupt-side).
// 0x00241F60
extern "C" int sceMc2iChangeThreadPriority(int prio)
{
    if (prio <= 0) {
        return 0x81010016;
    }
    iChangeThreadPriority(D_01D815E0.field_0, prio);
    return 0;
}

// [C] request command 3 (format): +0 = socket, +4 = 3, signal the worker; 0.
// 0x00241F98
extern "C" int func_00241F98(int socket)
{
    int r;

    r = mc2_begin(socket);
    if (r != 0) {
        return r;
    }
    D_01D810C0.field_0 = socket;
    D_01D810C0.field_4 = 3;
    func_002496A0();
    return 0;
}

// [C] request command 4 (unformat): as func_00241F98.
// 0x00242018
extern "C" int func_00242018(int socket)
{
    int r;

    r = mc2_begin(socket);
    if (r != 0) {
        return r;
    }
    D_01D810C0.field_0 = socket;
    D_01D810C0.field_4 = 4;
    func_002496A0();
    return 0;
}

// [C] request command 2 (get info): +0xC = a1, +0 = socket, +4 = 2.
// 0x00242098
extern "C" int sceMc2GetInfoAsync(int socket, unsigned int a1)
{
    int r;

    r = mc2_begin(socket);
    if (r != 0) {
        return r;
    }
    D_01D810C0.field_C = a1;
    D_01D810C0.field_0 = socket;
    D_01D810C0.field_4 = 2;
    func_002496A0();
    return 0;
}

// [C] request command 0xE (search file): strlen(name) >= 0x80: 0x8101005B; +0 = socket, +4 = 0xE, name copied to
// +0x41C, +0xC = a2.
// 0x00242128
extern "C" int func_00242128(int socket, const char *name, unsigned int a2)
{
    int r;

    r = mc2_begin(socket);
    if (r != 0) {
        return r;
    }
    if (strlen(name) >= 0x80) {
        return 0x8101005B;
    }
    D_01D810C0.field_0 = socket;
    D_01D810C0.field_4 = 0xE;
    strcpy(D_01D810C0.field_41C, name);
    D_01D810C0.field_C = a2;
    func_002496A0();
    return 0;
}

// [C] request command 0x64 (no worker case: the switch's default, it only signals done): +0 = socket, +4 = 0x64.
// 0x002421E8
extern "C" int func_002421E8(int socket)
{
    int r;

    r = mc2_begin(socket);
    if (r != 0) {
        return r;
    }
    D_01D810C0.field_0 = socket;
    D_01D810C0.field_4 = 0x64;
    func_002496A0();
    return 0;
}

// [C] request command 5: name checked / copied; +0xC = a2, +0x14 = a4, +0x10 = a3.
// 0x00242268
extern "C" int func_00242268(int socket, const char *name, unsigned int a2, unsigned int a3, unsigned int a4)
{
    int r;

    r = mc2_begin(socket);
    if (r != 0) {
        return r;
    }
    if (strlen(name) >= 0x80) {
        return 0x8101005B;
    }
    D_01D810C0.field_0 = socket;
    D_01D810C0.field_4 = 5;
    strcpy(D_01D810C0.field_41C, name);
    D_01D810C0.field_C = a2;
    D_01D810C0.field_14 = a4;
    D_01D810C0.field_10 = a3;
    func_002496A0();
    return 0;
}

// [C] request command 6: as func_00242268.
// 0x00242348
extern "C" int func_00242348(int socket, const char *name, unsigned int a2, unsigned int a3, unsigned int a4)
{
    int r;

    r = mc2_begin(socket);
    if (r != 0) {
        return r;
    }
    if (strlen(name) >= 0x80) {
        return 0x8101005B;
    }
    D_01D810C0.field_0 = socket;
    D_01D810C0.field_4 = 6;
    strcpy(D_01D810C0.field_41C, name);
    D_01D810C0.field_C = a2;
    D_01D810C0.field_14 = a4;
    D_01D810C0.field_10 = a3;
    func_002496A0();
    return 0;
}

// [C] request command 7: name checked / copied; +0xC = 0x8417.
// 0x00242428
extern "C" int func_00242428(int socket, const char *name)
{
    int r;

    r = mc2_begin(socket);
    if (r != 0) {
        return r;
    }
    if (strlen(name) >= 0x80) {
        return 0x8101005B;
    }
    D_01D810C0.field_0 = socket;
    D_01D810C0.field_4 = 7;
    strcpy(D_01D810C0.field_41C, name);
    D_01D810C0.field_C = 0x8417;
    func_002496A0();
    return 0;
}

// [C] request command 8: name checked / copied, no argument.
// 0x002424E0
extern "C" int func_002424E0(int socket, const char *name)
{
    int r;

    r = mc2_begin(socket);
    if (r != 0) {
        return r;
    }
    if (strlen(name) >= 0x80) {
        return 0x8101005B;
    }
    D_01D810C0.field_0 = socket;
    D_01D810C0.field_4 = 8;
    strcpy(D_01D810C0.field_41C, name);
    func_002496A0();
    return 0;
}

// [C] request command 0xA (get dir): name checked / copied; +0xC = a2, +0x18 = a5, +0x10 = a3, +0x14 = a4.
// 0x00242598
extern "C" int sceMc2GetDirAsync(int socket, const char *name, unsigned int a2, unsigned int a3, unsigned int a4,
                                 unsigned int a5)
{
    int r;

    r = mc2_begin(socket);
    if (r != 0) {
        return r;
    }
    if (strlen(name) >= 0x80) {
        return 0x8101005B;
    }
    D_01D810C0.field_0 = socket;
    D_01D810C0.field_4 = 0xA;
    strcpy(D_01D810C0.field_41C, name);
    D_01D810C0.field_C = a2;
    D_01D810C0.field_18 = a5;
    D_01D810C0.field_10 = a3;
    D_01D810C0.field_14 = a4;
    func_002496A0();
    return 0;
}

// [C] request command 0xB: name checked / copied; +0xC = 0x8427.
// 0x00242688
extern "C" int func_00242688(int socket, const char *name)
{
    int r;

    r = mc2_begin(socket);
    if (r != 0) {
        return r;
    }
    if (strlen(name) >= 0x80) {
        return 0x8101005B;
    }
    D_01D810C0.field_0 = socket;
    D_01D810C0.field_4 = 0xB;
    strcpy(D_01D810C0.field_41C, name);
    D_01D810C0.field_C = 0x8427;
    func_002496A0();
    return 0;
}

// [C] request command 0xC: name checked / copied; +0xC = a2.
// 0x00242740
extern "C" int func_00242740(int socket, const char *name, unsigned int a2)
{
    int r;

    r = mc2_begin(socket);
    if (r != 0) {
        return r;
    }
    if (strlen(name) >= 0x80) {
        return 0x8101005B;
    }
    D_01D810C0.field_0 = socket;
    D_01D810C0.field_4 = 0xC;
    strcpy(D_01D810C0.field_41C, name);
    D_01D810C0.field_C = a2;
    func_002496A0();
    return 0;
}

// [C] request command 0xD (chmod): a2 masked to 16 bits at entry; name checked / copied; +0xC = the masked a2.
// 0x00242800
extern "C" int func_00242800(int socket, const char *name, unsigned int a2)
{
    unsigned int mode;
    int r;

    mode = a2 & 0xFFFF;
    r = mc2_begin(socket);
    if (r != 0) {
        return r;
    }
    if (strlen(name) >= 0x80) {
        return 0x8101005B;
    }
    D_01D810C0.field_0 = socket;
    D_01D810C0.field_4 = 0xD;
    strcpy(D_01D810C0.field_41C, name);
    D_01D810C0.field_C = mode;
    func_002496A0();
    return 0;
}

// [C] request command 9 (rename): both names strlen-checked (name, then name2), +0 = socket, +4 = 9, name -> +0x41C,
// name2 -> +0x49C.
// 0x002428C0
extern "C" int func_002428C0(int socket, const char *name, const char *name2)
{
    int r;

    r = mc2_begin(socket);
    if (r != 0) {
        return r;
    }
    if (strlen(name) >= 0x80) {
        return 0x8101005B;
    }
    if (strlen(name2) >= 0x80) {
        return 0x8101005B;
    }
    D_01D810C0.field_0 = socket;
    D_01D810C0.field_4 = 9;
    strcpy(D_01D810C0.field_41C, name);
    strcpy(D_01D810C0.field_49C, name2);
    func_002496A0();
    return 0;
}

// [C] request command 0xF: name checked / copied.
// 0x002429A8
extern "C" int func_002429A8(int socket, const char *name)
{
    int r;

    r = mc2_begin(socket);
    if (r != 0) {
        return r;
    }
    if (strlen(name) >= 0x80) {
        return 0x8101005B;
    }
    D_01D810C0.field_0 = socket;
    D_01D810C0.field_4 = 0xF;
    strcpy(D_01D810C0.field_41C, name);
    func_002496A0();
    return 0;
}

// [C] the worker thread (entry of CreateThread): forever { func_00249680 (wait for a request); switch on
// (command - 2) with the jump table jtbl_004BB320 (99 entries: 16 cases for commands 2..0x11, the rest and any
// command outside 2..0x64 go to the default); store the result in +8; SignalSema(D_01D815E0.field_8) }.
// Cases (command: call, arguments from the request slot r):
//   2 _sceMcFatGetInfo(r0, +0xC); 3 _sceMcFatFormat(r0); 4 _sceMcFatUnFormat(r0);
//   5 func_00246588(r0, name, +0xC, +0x10, +0x14); 6 func_00246788(same); 7 and 0xB func_00245FC0(r0, name, +0xC);
//   8 func_00246DC8(r0, name); 9 _sceMcFatRename(r0, name, name2); 0xA _sceMcFatGetDir(r0, name, +0xC, +0x10,
//   +0x14, +0x18); 0xC func_00246FC8(r0, name, +0xC); 0xD func_00247580(r0, name, (u16)+0xC); 0xE search file
//   (below); 0xF func_00247808(r0, name); 0x10 _sceMcCoreReadPage2(r0, +0xC, +0x10, 1, +0x14); 0x11
//   _sceMcCoreWritePage(r0, +0xC, +0x10, +0x14).
// 0x00242A60
extern "C" void basic_thread(void)
{
    // boot2 E1: locals passed down to calls live on the guest stack at retail's offsets
    // [C 0x242A60 addiu sp, -0x240; daddu a2, sp (_sceMcFatSearchFile a2)]
    B3_RETAIL_FRAME(0x240);
    UnkStruct_McRequest *r;
    UnkStruct_McDirEnt &ent = *B3_FRAME_LOCAL(UnkStruct_McDirEnt, 0);   // sp+0
    unsigned char *dst;
    int i;

    r = &D_01D810C0;
    for (;;) {
        func_00249680();
        switch ((unsigned int)(r->field_4 - 2)) {
        case 0:
            r->field_8 = _sceMcFatGetInfo(r->field_0, r->field_C);
            break;
        case 1:
            r->field_8 = _sceMcFatFormat(r->field_0);
            break;
        case 2:
            r->field_8 = _sceMcFatUnFormat(r->field_0);
            break;
        case 3:
            r->field_8 = func_00246588(r->field_0, r->field_41C, r->field_C, r->field_10, r->field_14);
            break;
        case 4:
            r->field_8 = func_00246788(r->field_0, r->field_41C, r->field_C, r->field_10, r->field_14);
            break;
        case 5:
            r->field_8 = func_00245FC0(r->field_0, r->field_41C, r->field_C);
            break;
        case 6:
            r->field_8 = func_00246DC8(r->field_0, r->field_41C);
            break;
        case 7:
            r->field_8 = _sceMcFatRename(r->field_0, r->field_41C, r->field_49C);
            break;
        case 8:
            r->field_8 = _sceMcFatGetDir(r->field_0, r->field_41C, r->field_C, r->field_10, r->field_14,
                                         r->field_18);
            break;
        case 9:
            r->field_8 = func_00245FC0(r->field_0, r->field_41C, r->field_C);
            break;
        case 10:
            r->field_8 = func_00246FC8(r->field_0, r->field_41C, r->field_C);
            break;
        case 11:
            r->field_8 = func_00247580(r->field_0, r->field_41C, r->field_C & 0xFFFF);
            break;
        case 12:
            // [C] 0x00242C50: the result stored; 0x81010011 -> 0; 0x81019FFF -> 0x81010002; then a nonzero +0xC (the
            // caller's out entry) gets {+0..7 = ent+8, +8..0xF = ent+0x18 (unaligned doubleword copies), +0x10 =
            // ent+4 (word), +0x14 = ent+0 (halfword), name at +0x18 = ent+0x40}; the copy order is as the asm's.
            r->field_8 = _sceMcFatSearchFile(r->field_0, r->field_41C, &ent, 0, 0);
            if (r->field_8 == (int)0x81010011) {
                r->field_8 = 0;
            }
            if (r->field_8 == (int)0x81019FFF) {
                r->field_8 = (int)0x81010002;
            }
            dst = (unsigned char *)r->field_C;
            if (dst == 0) {
                break;
            }
            strcpy((char *)dst + 0x18, ent.field_40);
            for (i = 0; i < 8; i++) {
                dst[i] = ent.field_8[i];
            }
            for (i = 0; i < 8; i++) {
                dst[8 + i] = ent.field_18[i];
            }
            *(unsigned int *)(dst + 0x10) = ent.field_4;
            *(unsigned short *)(dst + 0x14) = ent.field_0;
            break;
        case 13:
            r->field_8 = func_00247808(r->field_0, r->field_41C);
            break;
        case 14:
            r->field_8 = _sceMcCoreReadPage2(r->field_0, r->field_C, r->field_10, 1, r->field_14);
            break;
        case 15:
            r->field_8 = _sceMcCoreWritePage(r->field_0, r->field_C, r->field_10, r->field_14);
            break;
        default:
            break;
        }
        SignalSema(D_01D815E0.field_8);
    }
}

// [C] no request pending (+4 == 0): -1. Else *a1 = the command (when a1 != 0); a nonzero first argument polls
// D_01D815E8 (PollSema < 0: 0), zero waits (WaitSema); then *a2 = the result (when a2 != 0); *a1 = +4 (when a1 !=
// 0, read again) and +4 = 0; returns 1.
// 0x00242D60
extern "C" int sceMc2Sync2(int nowait, int *a1, int *a2)
{
    int cmd;

    cmd = D_01D810C0.field_4;
    if (cmd == 0) {
        return -1;
    }
    if (a1 != 0) {
        *a1 = cmd;
    }
    if (nowait == 0) {
        WaitSema(D_01D815E8);
    } else {
        if (PollSema(D_01D815E8) < 0) {
            return 0;
        }
    }
    if (a2 != 0) {
        *a2 = D_01D810C0.field_8;
    }
    if (a1 == 0) {
        D_01D810C0.field_4 = 0;
    } else {
        *a1 = D_01D810C0.field_4;
        D_01D810C0.field_4 = 0;
    }
    return 1;
}

// [C] tail call sceMc2Sync2(1, a0, a1) (the polling form).
// 0x00242E00
extern "C" int func_00242E00(int *a0, int *a1)
{
    return sceMc2Sync2(1, a0, a1);
}

// [C] tail call sceMc2Sync2(0, a0, a1) (the waiting form).
// 0x00242E28
extern "C" int sceMc2Sync(int *a0, int *a1)
{
    return sceMc2Sync2(0, a0, a1);
}

// [C] the synchronous wrappers below (0x00242E50..0x00243190): the *Async function with the caller's arguments, then
// when it returned 0 sceMc2Sync2(0, 0, &result) (result overwritten by the worker's), returning the result.
// 0x00242E50
extern "C" int sceMc2GetInfo(int socket, unsigned int a1)
{
    return mc2_wait(sceMc2GetInfoAsync(socket, a1));
}

// 0x00242E90
extern "C" int sceMc2Format(int socket)
{
    return mc2_wait(func_00241F98(socket));
}

// 0x00242ED0
extern "C" int sceMc2Unformat(int socket)
{
    return mc2_wait(func_00242018(socket));
}

// 0x00242F10
extern "C" int sceMc2ReadFile(int socket, const char *name, unsigned int a2, unsigned int a3, unsigned int a4)
{
    return mc2_wait(func_00242268(socket, name, a2, a3, a4));
}

// 0x00242F50
extern "C" int func_00242F50(int socket, const char *name, unsigned int a2, unsigned int a3, unsigned int a4)
{
    return mc2_wait(func_00242348(socket, name, a2, a3, a4));
}

// 0x00242F90
extern "C" int func_00242F90(int socket, const char *name)
{
    return mc2_wait(func_00242428(socket, name));
}

// 0x00242FD0
extern "C" int func_00242FD0(int socket, const char *name)
{
    return mc2_wait(func_002424E0(socket, name));
}

// 0x00243010
extern "C" int func_00243010(int socket, const char *name, const char *name2)
{
    return mc2_wait(func_002428C0(socket, name, name2));
}

// 0x00243050
extern "C" int func_00243050(int socket, const char *name)
{
    return mc2_wait(func_00242688(socket, name));
}

// 0x00243090
extern "C" int func_00243090(int socket, const char *name, unsigned int a2)
{
    return mc2_wait(func_00242740(socket, name, a2));
}

// 0x002430D0
extern "C" int func_002430D0(int socket, const char *name, unsigned int a2, unsigned int a3, unsigned int a4,
                             unsigned int a5)
{
    return mc2_wait(sceMc2GetDirAsync(socket, name, a2, a3, a4, a5));
}

// [C] the mode argument (a2) masked to 16 bits before the async call (func_00242800 masks it again).
// 0x00243110
extern "C" int sceMc2Chmod(int socket, const char *name, unsigned int a2)
{
    return mc2_wait(func_00242800(socket, name, a2 & 0xFFFF));
}

// 0x00243150
extern "C" int sceMc2SearchFile(int socket, const char *name, unsigned int a2)
{
    return mc2_wait(func_00242128(socket, name, a2));
}

// 0x00243190
extern "C" int sceMc2GetEntSpace(int socket, const char *name)
{
    return mc2_wait(func_002429A8(socket, name));
}

// [C] strchr-like: the index of the first byte equal to the (sign-extended) character, or -1 (the terminating zero
// matches only when the character is 0, giving the length).
// 0x002431D0
extern "C" int func_002431D0(const char *str, int c)
{
    const signed char *p;
    signed char ch;

    ch = (signed char)c;
    p = (const signed char *)str;
    while (*p != 0 && *p != ch) {
        p++;
    }
    if (*p != ch) {
        return -1;
    }
    return (int)(p - (const signed char *)str);
}

// [C] 1 when the part of the path after its last '/' (or the whole string when it has none) is shorter than 0x20.
// 0x00243238
extern "C" int isCorrNameSize(const char *path)
{
    const char *p;   // mac2: clang's strrchr(const char *) returns const char * (GCC -fpermissive only warned)

    if (strchr(path, 0x2F) == 0) {
        return strlen(path) < 0x20;
    }
    p = strrchr(path, 0x2F);
    return strlen(p + 1) < 0x20;
}

// [C] 0 when a byte is '?' or '*' or a (signed) control character below 0x20, else 1 at the terminating zero.
// 0x002432A8
extern "C" int isCorrFname(const char *name)
{
    const signed char *p;

    p = (const signed char *)name;
    while (*p != 0) {
        if (*p == 0x3F || *p == 0x2A || *p < 0x20) {
            return 0;
        }
        p++;
    }
    return 1;
}

// ===================================================================================================================
// Part 2 of 3 (functions 44 to 86 of the file, in file order): func_00243300 .. func_00247C00. Written boot2
// (2026-11-06). The path helpers and the FAT layer (cluster chains over the two page caches of D_01D8D708), the
// directory entry layer, format / unformat, rename, getdir, and the sceMcp* card access layer.
// ===================================================================================================================

// [C] one per-socket FAT context, 0x898 bytes, array D_01D8D708 (socket * 0x898; the symbol table splits it into
// D_01D8D708, D_01D8DB04/08, D_01D8DF04/10/90/94/98: all one array of these).
struct UnkStruct_McFatCtx {
    unsigned int field_0[0x100];    // [C] cache 1: the 256 entries of the indirect FAT page loaded in field_804 (func_002438B8)
    unsigned int field_400[0x100];  // [C] cache 2: the 256 FAT entries of the page loaded in field_800 (D_01D8DB08 + socket * 0x898)
    int field_800;                  // [C] FAT page number cached in field_400 (-1 none)
    int field_804;                  // [C] indirect page number cached in field_0 (-1 none)
    char field_808[0x80];           // [C] the current directory path (D_01D8DF10 + socket * 0x898), "/" after init
    int field_888;                  // [C] 1 after init, set 1 on a fresh card, cleared when GetInfo reports the change
    int field_88C;                  // [C] 1 when the card is a valid formatted one (set by GetInfo)
    int field_890;                  // [C] the free cluster search start (D_01D8DF98 + socket * 0x898)
    int field_894;                  // [C] 1 when cache 2 is dirty
};


// [C] _sceMcCoreGetCardSpec's result block (func_002478D8 is a tail call to it): page size, pages per cluster,
// page count ([L] names).
struct UnkStruct_McGeom {
    int field_0;
    int field_4;
    int field_8;
};

extern UnkStruct_McFatCtx D_01D8D708[];     // [C] 0x01D8D708 (.bss, 2 sockets)
extern char D_01D8D608[];                   // [C] 0x01D8D608 (.bss, 0x100): _strtok's copy of the string
extern char *D_01D8D600;                    // [C] 0x01D8D600 (.bss): _strtok's continuation pointer
extern char D_01D8E840[];                   // [C] 0x01D8E840 (.bss, 0x400): the 2 directory entries of the last read cluster
extern char D_0049B6F0[];                   // [C] 0x0049B6F0 (.data): per-socket card parameter block, 0x184 bytes each
extern char D_004BB4B0[];                   // [C] 0x004BB4B0 (rodata) "."
extern char D_004BB4B8[];                   // [C] 0x004BB4B8 (rodata) ".."
extern char D_004BB4C0[];                   // [C] 0x004BB4C0 (rodata) "\""
extern char D_004BB4C8[];                   // [C] 0x004BB4C8 (rodata) "/"
extern char D_004BB4D0[];                   // [C] 0x004BB4D0 (rodata) "Sony PS2 Memory Card Format"

extern "C" int strcmp(const char *a, const char *b);
extern "C" int strncmp(const char *a, const char *b, unsigned int n);
extern "C" char *strcat(char *dst, const char *src);

// [C] callees outside part 2 (part 3 defines them) and the libdbc one
extern "C" int func_00249640(int socket);           // [C] sceDbcGetConnection(D_01D9348C + socket * 0x1C) != 3
extern "C" int func_00248188(int socket, void *buf, unsigned int clust, unsigned int a3);
extern "C" int func_00248210(int socket, int clust);
extern "C" int func_00248828(int socket, const void *params);  // [C] copies 16 bytes to D_01D93490 + socket * 0x1C, returns 1
extern "C" int _sceMcCoreWriteBack(int socket);
extern "C" int func_00248A68(int socket);
extern "C" int _sceMcCoreCheckNewCard(int socket);
extern "C" int func_00249160(int socket, void *buf, unsigned int page, unsigned int count);
extern "C" int _sceMcCoreBlockErase(int socket, int block);
extern "C" int isCorrFname(const char *name);
extern "C" int isCorrNameSize(const char *path);
extern "C" int func_00245040(int socket, char *path, char *out);
extern "C" int func_002436C0(int socket, char *path, char *out);

// part 2's own functions used before their definition
extern "C" int func_002431D0(const char *str, int c);
extern "C" int isWildFit(const char *str, const char *pat);
extern "C" char *_strtok(char *str, const char *delim);
extern "C" int _sceMcFatReadFatData(int socket, int clust, int *err);
extern "C" int _sceMcFatFlushFatCache(int socket);
extern "C" int _sceMcFatGetNextClust(int socket, int clust, int *err);
extern "C" int _sceMcFatGetFreeClustSize(int socket, int *err);
extern "C" int _sceMcFatWriteFatData(int socket, int clust, unsigned int value, int *err);
extern "C" int _GetEntryByPos(int socket, char *buf, int clust, int pos);
extern "C" int func_00244118(int socket, int clust);
extern "C" int func_002438B8(int socket, int clust, int *err);
extern "C" int func_00247B70(int socket, void *buf, unsigned int clust, unsigned int a3);
extern "C" int func_00247BB8(int socket, void *buf, unsigned int clust, unsigned int a3);
extern "C" int func_002478D8(int socket, UnkStruct_McGeom *out);
extern "C" int func_002478F0(int socket);
extern "C" int func_00247908(int socket);
extern "C" int _sceMcpReadSysInfo(int socket);
extern "C" int _sceMcpWriteSysInfo(int socket);
extern "C" int _SearchEntry(int socket, char *out, int *posOut, const char *name, int clust, int count);
extern "C" int _SetEntryByPos(int socket, char *ent, int clust, int pos);
extern "C" int _GetFreeEntrySize(int socket, int clust, int count, int *err);
extern "C" int _CreateEntry(int socket, char *newEnt, int clust, int count, int *err);
extern "C" int _reset_problem_patch(int socket, char *info);
extern "C" int _sceMcFatWriteFatData(int socket, int clust, unsigned int value, int *err);
extern "C" int _sceMcFatGetFreeClust(int socket, int *err);
extern "C" int func_002481A0(int socket, void *buf, int clust, int count);
extern "C" int func_00247C00(int socket);

// [C] the parameter block of socket s at byte offset off (D_0049B6F0 + s * 0x184 + off)
static inline int *mc2_param(int socket, int off)
{
    return (int *)(D_0049B6F0 + socket * 0x184 + off);
}

// [C] the leaf of a path: true (1) when the part after the last '/' (or the whole string) is shorter than 0x20 and is
// neither "." nor "..".
// 0x00243300
extern "C" int func_00243300(const char *path)
{
    // boot2 E1: locals passed down to calls live on the guest stack at retail's offsets
    // [C 0x243300 addiu sp, -0x30; daddu a0, sp (strcpy / strcmp a0)]
    B3_RETAIL_FRAME(0x30);
    char *buf = B3_FRAME_LOCAL(char, 0);   // [C] sp+0, 0x20 bytes
    const char *p;

    if (strchr(path, 0x2F) == 0) {
        if (strlen(path) >= 0x20) {
            return 0;
        }
        strcpy(buf, path);
    } else {
        p = strrchr(path, 0x2F);
        if (strlen(p + 1) >= 0x20) {
            return 0;
        }
        p = strrchr(path, 0x2F);
        strcpy(buf, p + 1);
    }
    if (strcmp(buf, D_004BB4B0) != 0 && strcmp(buf, D_004BB4B8) != 0) {
        return 1;
    }
    return 0;
}

// [C] wildcard match of str against pat ('?' = one character, '*' = any run): returns 1 on a match, 0 otherwise.
// func_002431D0 is the index-of helper.
// 0x002433D0
extern "C" int isWildFit(const char *str, const char *pat)
{
    int q;      // s2: index of the first '?' in pat
    int st;     // v1: index of the first '*'
    int n;      // s0
    int idx;
    signed char c;

    for (;;) {
        q = func_002431D0(pat, 0x3F);
        st = func_002431D0(pat, 0x2A);
        if (q >= 0) {
            n = q;
            if (st >= 0 && !(q < st)) {
                n = st;
            }
        } else if (st >= 0) {
            n = st;
        } else {
            return strcmp(pat, str) == 0;
        }
        if (strncmp(pat, str, n) != 0) {
            return 0;
        }
        pat += n;
        str += n;
        if (*pat == 0x3F) {
            // one string character per '?'; the string ending first is a match
            do {
                if (*str++ == 0) {
                    return 1;
                }
                pat++;
            } while (*pat == 0x3F);
        }
        if (*pat != 0x2A) {
            continue;
        }
        // '*': skip any run of '*' / '?' in the pattern
        pat++;
        for (;;) {
            c = *pat;
            if (c == 0x2A) {
                pat++;
            } else if (c == 0x3F) {
                pat++;
            } else {
                break;
            }
        }
        if (c == 0) {
            return 1;
        }
        // try every position of the string where the next pattern character occurs
        for (;;) {
            idx = func_002431D0(str, (signed char)*pat);
            str += idx;
            if (idx < 0) {
                return 0;
            }
            if (isWildFit(str, pat) == 1) {
                return 1;
            }
            str++;
        }
    }
}

// [C] strtok-like: with a string, copies it into D_01D8D608 and tokenises the copy; with 0 continues at D_01D8D600.
// Leading delimiters are skipped, a token starting with '"' runs to the next '"' (the quote itself is not part of it).
// Returns the token (terminated in place) or 0.
// 0x00243558
extern "C" char *_strtok(char *str, const char *delim)
{
    char *s0;
    char *tok;      // s2
    char *p;

    s0 = str;
    if (s0 != 0) {
        strcpy(D_01D8D608, s0);
        s0 = D_01D8D608;
    } else {
        s0 = D_01D8D600;
        if (s0 == 0) {
            return 0;
        }
    }
    if (*s0 == 0) {
        return 0;
    }
    // skip delimiters (strchr also matches the terminating zero)
    for (;;) {
        signed char ch = *s0;
        s0++;
        if (strchr(delim, ch) == 0) {
            break;
        }
        if (s0[-1] == 0) {
            return 0;
        }
    }
    tok = s0 - 1;
    if (*tok == 0x22) {
        tok = s0;
        do {
            signed char ch = *s0;
            s0++;
            if (strchr(D_004BB4C0, ch) != 0) {
                break;
            }
        } while (1);
    } else {
        do {
            signed char ch = *s0;
            s0++;
            if (strchr(delim, ch) != 0) {
                break;
            }
        } while (1);
    }
    p = s0 - 1;
    if (*p == 0) {
        D_01D8D600 = 0;
    } else {
        D_01D8D600 = s0;
    }
    *p = 0;
    return tok;
}

// [C] the number of '/' characters in the string.
// 0x00243678
extern "C" int _get_dir_nest(const char *path)
{
    int n;
    const char *p;

    n = 0;
    p = path;
    if (*p != 0) {
        do {
            if (*p == 0x2F) {
                n = n + 1;
            }
            p++;
        } while (*p != 0);
    }
    return n;
}

// [C] resolves path (relative to the socket's current directory field_808, or absolute) into out: copies the current
// directory, then walks the '/'-separated parts: "." is dropped, ".." removes the last component, anything else is
// appended with a '/'; a trailing '/' is cut unless the result is "/". Returns 1, or 0 when the path is 0x80 bytes or
// longer or ".." goes above the root.
// 0x002436C0
extern "C" int func_002436C0(int socket, char *path, char *out)
{
    char *tok;
    char *p;

    if (strlen(path) >= 0x80) {
        return 0;
    }
    strcpy(out, D_01D8D708[socket].field_808);
    if (*path == 0x2F) {
        out[0] = D_004BB4C8[0];
        out[1] = D_004BB4C8[1];
        tok = _strtok(path + 1, D_004BB4C8);
    } else {
        tok = _strtok(path, D_004BB4C8);
    }
    while (tok != 0) {
        if (strcmp(tok, D_004BB4B0) != 0) {
            if (strcmp(tok, D_004BB4B8) == 0) {
                // ".."
                out[strlen(out) - 1] = 0;
                p = strrchr(out, 0x2F);
                if (p == 0) {
                    return 0;
                }
                p[1] = 0;
            } else {
                strcat(out, tok);
                strcat(out, D_004BB4C8);
            }
        }
        tok = _strtok(0, D_004BB4C8);
    }
    if (strcmp(out, D_004BB4C8) != 0) {
        p = strrchr(out, 0x2F);
        *p = 0;     // [C] no null test in the asm (out always holds a '/')
    }
    return 1;
}

// [C] initialises the 2 FAT contexts: both page tags -1, dirty 0, field_888 1, field_88C 0, field_890 0, current
// directory "/". Returns 1.
// 0x00243858
extern "C" int _sceMcFatInit(void)
{
    int i;

    for (i = 0; i < 2; i++) {
        D_01D8D708[i].field_800 = -1;
        D_01D8D708[i].field_804 = -1;
        D_01D8D708[i].field_894 = 0;
        D_01D8D708[i].field_888 = 1;
        D_01D8D708[i].field_88C = 0;
        D_01D8D708[i].field_890 = 0;
        D_01D8D708[i].field_808[0] = D_004BB4C8[0];
        D_01D8D708[i].field_808[1] = D_004BB4C8[1];
    }
    return 1;
}

// [C] the FAT cluster number holding the chain entry of cluster clust (an indirect-FAT lookup): loads the 64K-entry
// page clust >> 16 of the indirect table (func_00247B70, cluster number from the parameter block at +0x50 + page * 4)
// into cache 1 when it is not the cached one (field_804), then returns cache1[(clust >> 8) - (page << 8)]. *err = 0, or
// error 0x8101006F (read failed, returns 0) or 0x81019001 (entry negative, returns 0).
// 0x002438B8
extern "C" int func_002438B8(int socket, int clust, int *err)
{
    UnkStruct_McFatCtx *ctx;
    int page;
    int idx;
    int v;

    ctx = &D_01D8D708[socket];
    page = (clust < 0 ? clust + 0xFFFF : clust) >> 16;
    idx = ((clust < 0 ? clust + 0xFF : clust) >> 8) - (page << 8);
    if (ctx->field_804 != page) {
        if (func_00247B70(socket, ctx, (unsigned int)*mc2_param(socket, 0x50 + page * 4), 1) == 0) {
            *err = 0x8101006F;
            return 0;
        }
        ctx->field_804 = page;
    }
    v = (int)ctx->field_0[idx];
    if (v >= 0) {
        *err = 0;
        return v;
    }
    *err = 0x81019001;
    return 0;
}

// [C] writes cache 2 back (func_00247BB8 to the cached page field_800) when it is loaded (> 0) and dirty (field_894 ==
// 1), then clears the dirty flag. Returns 1, or 0 when the write failed (flag left set).
// 0x002439E0
extern "C" int _sceMcFatFlushFatCache(int socket)
{
    UnkStruct_McFatCtx *ctx;

    ctx = &D_01D8D708[socket];
    if (ctx->field_800 > 0 && ctx->field_894 == 1) {
        if (func_00247BB8(socket, ctx->field_400, ctx->field_800, 1) == 0) {
            return 0;
        }
    }
    ctx->field_894 = 0;
    return 1;
}

// [C] reads FAT entry clust: finds its FAT page via func_002438B8, loads that page into cache 2 when it is not the
// cached one (flushing the old one first; the asm also writes the dirty page a second time, to the new page number, as
// an inlined repeat of the flush), and returns cache2[clust - (clust >> 8 << 8)]. *err = 0 or 0x8101006F (returns 0).
// 0x00243A68
extern "C" int _sceMcFatReadFatData(int socket, int clust, int *err)
{
    UnkStruct_McFatCtx *ctx;
    int idx;
    int page;

    idx = clust - (((clust < 0) ? clust + 0xFF : clust) >> 8 << 8);
    page = func_002438B8(socket, clust, err);
    if (*err != 0) {
        return 0;
    }
    ctx = &D_01D8D708[socket];
    if (ctx->field_800 != page) {
        if (_sceMcFatFlushFatCache(socket) == 0) {
            *err = 0x8101006F;
            return 0;
        }
        if (ctx->field_800 > 0 && ctx->field_894 == 1) {
            if (func_00247BB8(socket, ctx->field_400, page, 1) == 0) {
                *err = 0x8101006F;
                return 0;
            }
        }
        if (func_00247B70(socket, ctx->field_400, page, 1) == 0) {
            *err = 0x8101006F;
            return 0;
        }
        ctx->field_800 = page;
    }
    *err = 0;
    return (int)ctx->field_400[idx];
}

// [C] the next cluster of the chain: _sceMcFatReadFatData; 0x7FFFFFFF is an error (*err = 0x81019002).
// 0x00243BD8
extern "C" int _sceMcFatGetNextClust(int socket, int clust, int *err)
{
    int v;

    v = _sceMcFatReadFatData(socket, clust, err);
    if (v == 0x7FFFFFFF) {
        *err = 0x81019002;
    }
    return v;
}

// [C] writes FAT entry clust = value into cache 2 (loading its page first, as _sceMcFatReadFatData) and marks the
// cache dirty. Returns 1; *err = 0 or 0x8101006F (returns 0).
// 0x00243C18
extern "C" int _sceMcFatWriteFatData(int socket, int clust, unsigned int value, int *err)
{
    UnkStruct_McFatCtx *ctx;
    int idx;
    int page;

    idx = clust - (((clust < 0) ? clust + 0xFF : clust) >> 8 << 8);
    page = func_002438B8(socket, clust, err);
    if (*err != 0) {
        return 0;
    }
    ctx = &D_01D8D708[socket];
    if (ctx->field_800 != page) {
        if (_sceMcFatFlushFatCache(socket) == 0) {
            *err = 0x8101006F;
            return 0;
        }
        if (func_00247B70(socket, ctx->field_400, page, 1) == 0) {
            *err = 0x8101006F;
            return 0;
        }
        ctx->field_800 = page;
    }
    D_01D8D708[socket].field_400[idx] = value;
    D_01D8D708[socket].field_894 = 1;
    *err = 0;
    return 1;
}

// [C] finds a free cluster: scans the FAT from the socket's search start field_890 up to the cluster count
// (parameter block +0x170); a free entry is one without bit 31. Wraps once to 0 (0x8101001C when a second pass finds
// nothing, i.e. the start was already 0). Stores the found cluster in field_890 and returns it, *err = 0.
// 0x00243D60
extern "C" int _sceMcFatGetFreeClust(int socket, int *err)
{
    int *start;
    int limit;
    int s0;
    int v;

    start = &D_01D8D708[socket].field_890;
    *err = 0;
    limit = *mc2_param(socket, 0x170);
    for (;;) {
        s0 = *start;
        if (s0 < limit) {
            for (;;) {
                v = _sceMcFatReadFatData(socket, s0, err);
                if (*err != 0) {
                    return 0;
                }
                if ((v & 0x80000000) == 0) {
                    *err = 0;
                    *start = s0;
                    return s0;
                }
                limit = *mc2_param(socket, 0x170);
                s0++;
                if (!(s0 < limit)) {
                    break;
                }
            }
        }
        if (*start == 0) {
            *err = 0x8101001C;
            return 0;
        }
        *start = 0;
    }
}

// [C] counts the free clusters (entries without bit 31) from the search start field_890 to the cluster count
// (parameter block +0x170). *err = 0 or the read error (returns 0).
// 0x00243EA8
extern "C" int _sceMcFatGetFreeClustSize(int socket, int *err)
{
    int s0;
    int count;
    int v;

    *err = 0;
    s0 = D_01D8D708[socket].field_890;
    count = 0;
    if (s0 < *mc2_param(socket, 0x170)) {
        do {
            v = _sceMcFatReadFatData(socket, s0, err);
            s0++;
            if ((v & 0x80000000) == 0) {
                count++;
            }
            if (*err != 0) {
                return 0;
            }
        } while (s0 < *mc2_param(socket, 0x170));
    }
    return count;
}

// [C] copies the directory entry number pos (counted from cluster clust along the chain; 2 entries of 0x200 bytes per
// cluster) into buf (0x200 bytes) and zero-terminates its name (byte 0x5F). Returns 0, 0x8101006F (cluster read
// failed), the chain error, or 0x81010002 (chain ends: next cluster -1).
// 0x00243F80
extern "C" int _GetEntryByPos(int socket, char *buf, int clust, int pos)
{
    int err;
    int next;
    int i;
    const char *src;

    err = 0;
    for (;;) {
        if (pos < 2) {
            if (func_00248188(socket, D_01D8E840, clust, 1) == 0) {
                return 0x8101006F;
            }
            src = D_01D8E840 + (pos << 9);
            for (i = 0; i < 0x200; i++) {
                buf[i] = src[i];
            }
            buf[0x5F] = 0;
            return 0;
        }
        next = _sceMcFatGetNextClust(socket, clust, &err);
        clust = next;
        if (err != 0) {
            return err;
        }
        if (next == -1) {
            return 0x81010002;
        }
        clust = next & 0x7FFFFFFF;
        pos = pos - 2;
    }
}

// [C] the length of the cluster chain starting at clust: 0 for -1; a chain error is returned as the value; a link
// without bit 31 that is not -1 gives 0x81019002.
// 0x00244118
extern "C" int func_00244118(int socket, int clust)
{
    int err;
    int next;
    int count;
    int ret;

    count = 1;
    if (clust == -1) {
        return 0;
    }
    for (;;) {
        next = _sceMcFatGetNextClust(socket, clust, &err);
        if (err != 0) {
            return err;
        }
        ret = count;
        count++;
        if (next == -1) {
            return ret;
        }
        if ((next & 0x80000000) == 0) {
            return 0x81019002;
        }
        clust = next & 0x7FFFFFFF;
    }
}

// [C] 16-byte parameter block for func_00248828: lhu +0x28 (page size), lhu +0x2C, (u32 at +0x30) << 10 divided by the
// page size, 1; sent to the socket's record.
// 0x002441C0
extern "C" int _reset_problem_patch(int socket, char *info)
{
    // boot2 E1: locals passed down to calls live on the guest stack at retail's offsets
    // [C 0x2441C0 addiu sp, -0x20; daddu a1, sp (func_00248828 a1)]
    B3_RETAIL_FRAME(0x20);
    int *params = B3_FRAME_LOCAL(int, 0);   // sp+0, 4 words
    int size;

    size = (int)(*(unsigned short *)(info + 0x28));
    params[0] = size;
    params[1] = (int)(*(unsigned short *)(info + 0x2C));
    // [C] asm: div by size with `break 0, 7` when size == 0 (not expressed: the quotient is left 0 then)
    params[2] = (size != 0) ? (*(int *)(info + 0x30) << 10) / size : 0;
    params[3] = 1;
    return func_00248828(socket, params);
}

// [C] card information: info (may be 0) gets { type, formatted-flag, free clusters }. Needs a connection (func_00249640
// else 0x81010013). Reads the card's sys info when func_002478F0 says so (0 -> 0x8101006F), (re)initialises the FAT
// context when field_888 is set, checks the "Sony PS2 Memory Card Format" magic and version, counts the usable
// clusters, validates the root's "." and ".." entries. Errors 0x8101006F / 0x8101002F (not formatted) / 0x81010086
// (version too old) / chain errors; 0x81019003 when field_888 was set (cleared) and everything else passed.
// 0x00244218
extern "C" int _sceMcFatGetInfo(int socket, unsigned int a1)
{
    // boot2 E1: locals passed down to calls live on the guest stack at retail's offsets
    // [C 0x244218 addiu sp, -0x270; daddu a1, sp (func_002478D8 a1); addiu s2, sp, 0x10 (_GetEntryByPos a1)]
    B3_RETAIL_FRAME(0x270);
    int *info;                  // s5
    char *tbl;                  // s1: the parameter block of the socket
    UnkStruct_McFatCtx *ctx;
    int v;
    int type;                   // s4
    UnkStruct_McGeom &geom = *B3_FRAME_LOCAL(UnkStruct_McGeom, 0);   // sp+0 (_sceMcCoreGetCardSpec writes 16 bytes: up to sp+0x10)
    char *ent = B3_FRAME_LOCAL(char, 0x10);   // sp+0x10, 0x200 bytes
    int err;                    // sp+0x210
    int s0;
    int s2;
    int s4;
    int s6;
    int n;
    char *name;                 // sp+0x50: the entry's name (entry + 0x40)

    info = (int *)a1;
    tbl = D_0049B6F0 + socket * 0x184;
    if (func_00249640(socket) == 0) {
        return 0x81010013;
    }
    if (info != 0) {
        info[1] = 0;
        info[0] = 0;
        info[2] = 0;
    }
    v = func_002478F0(socket);
    if (v == 0) {
        return 0x8101006F;
    }
    type = 2;
    if (v == 2) {
        D_01D8D708[socket].field_888 = 1;
    }
    ctx = &D_01D8D708[socket];
    if (ctx->field_888 != 0) {
        if (func_002478D8(socket, &geom) == 0) {
            return 0x8101006F;
        }
        if (geom.field_0 == 0) {
            return 0x8101006F;
        }
        ctx->field_894 = 0;
        ctx->field_800 = -1;
        ctx->field_804 = -1;
        ctx->field_890 = 0;
        ctx->field_808[0] = D_004BB4C8[0];
        ctx->field_808[1] = D_004BB4C8[1];
        if (_sceMcpReadSysInfo(socket) == 0) {
            return 0x8101006F;
        }
        if (info != 0) {
            info[0] = type;
        }
        if (strncmp(tbl, D_004BB4D0, 0x1B) != 0) {
            if (info != 0) {
                info[1] = 0;
            }
            return 0x8101002F;
        }
        // [C] version digits at +0x1C / +0x1E of " 1.2.0.0": 10 * c1C + c1E - 0x210 must be at least 11
        v = ((signed char)tbl[0x1C]) * 10 + (signed char)tbl[0x1E];
        if (v - 0x210 < 0xB) {
            return 0x81010086;
        }
        _reset_problem_patch(socket, tbl);
        _sceMcCoreWriteBack(socket);
        s0 = *(int *)(tbl + 0x34);
        v = *(int *)(tbl + 0x30) / 0x3E8;
        s2 = 0;
        s4 = 0;
        s6 = v * 0x3E8 + 1;
        if (*(int *)(tbl + 0x38) > 0) {
            do {
                if (s2 == s6) {
                    *(int *)(tbl + 0x170) = s0 - *(int *)(tbl + 0x34);
                }
                n = s0;
                s0++;
                v = func_00248210(socket, n);
                s4++;
                if (v == 0) {
                    s2 = s2 + 1;
                }
            } while (s4 < *(int *)(tbl + 0x38));
        }
        err = _GetEntryByPos(socket, ent, *(int *)(tbl + 0x3C), 0);
        if (err != 0) {
            return err;
        }
        if ((*(unsigned short *)(ent + 0)) & 0x8000) {
            // fall on
        } else {
            return 0x8101002F;
        }
        name = ent + 0x40;
        if (strcmp(name, D_004BB4B0) != 0) {
            return 0x8101002F;
        }
        err = _GetEntryByPos(socket, ent, *(int *)(tbl + 0x3C), 1);
        if (err != 0) {
            return err;
        }
        if (((*(unsigned short *)(ent + 0)) & 0x8000) == 0) {
            return 0x8101002F;
        }
        if (strcmp(name, D_004BB4B8) != 0) {
            return 0x8101002F;
        }
        D_01D8D708[socket].field_88C = 1;
    }
    ctx = &D_01D8D708[socket];
    if (ctx->field_88C == 0) {
        return 0x8101002F;
    }
    if (info != 0) {
        n = _sceMcFatGetFreeClustSize(socket, &err);
        if (err != 0) {
            return err;
        }
        info[0] = 2;
        info[1] = ctx->field_88C;
        info[2] = n;
    }
    ctx = &D_01D8D708[socket];
    if (ctx->field_888 == 0) {
        return 0;
    }
    ctx->field_888 = 0;
    return 0x81019003;
}

// [C] R5900 `div` / `divu` by zero do not trap: LO = (rs >= 0 ? -1 : 1) for div, -1 for divu (the asm tests only some of
// its divisors with `break 0, 7`, those tests are not expressed here, see the report).
static inline int mc2_div(int a, int b)
{
    if (b == 0) {
        return (a >= 0) ? -1 : 1;
    }
    if (b == -1 && a == (int)0x80000000) {
        return a;
    }
    return a / b;
}

static inline unsigned int mc2_divu(unsigned int a, unsigned int b)
{
    if (b == 0) {
        return 0xFFFFFFFFu;
    }
    return a / b;
}

extern "C" int _sceMcCoreGetCardSpec(int socket, UnkStruct_McGeom *out);
extern "C" int func_00248268(int socket, UnkStruct_McGeom *geom, int *badlist);
extern "C" int func_002481B8(int socket, int clust);
extern "C" int _sceMcCoreReadClock(int socket, unsigned char *out);
extern "C" void *memset(void *p, int c, unsigned int n);
extern char D_004BB4F0[];                   // [C] 0x004BB4F0 (rodata) " 1.2.0.0"
extern char D_004BB500[];                   // [C] 0x004BB500 (rodata) "libmc2: Fatal Error [%08x]\n"

// [C] BCD byte to decimal, as the asm: (b >> 4) * 10 + (b & 15)
static inline unsigned int mc2_bcd(unsigned int b)
{
    return (b >> 4) * 10 + (b & 0xF);
}

// [C] formats the card: needs a connection (else 0x81010013). Resets the FAT contexts, reads the card spec and sys
// info, (re)builds the superblock in the parameter block (magic + " 1.2.0.0", geometry, the indirect FAT cluster
// list, the FAT page clusters, allocation start, cluster counts), writes the FAT pages, marks bad blocks (-3) and
// free ones (0x7FFFFFFF), creates the root directory ("." and ".." in cluster 0), flushes, writes the sys info.
// Returns 0 on success, 0x8101006F on card errors, 0x81019001 when a free cluster remains reserved, or an error code
// of the FAT layer.
// 0x002445D0
extern "C" int _sceMcFatFormat(int socket)
{
    // boot2 E1: locals passed down to calls live on the guest stack at retail's offsets
    // [C 0x2445D0 addiu sp, -0x80; daddu a1, sp (func_002478D8 / func_00248268 a1);
    // addiu a1, sp, 0x10 (_sceMcCoreReadClock a1)]
    B3_RETAIL_FRAME(0x80);
    char *tbl;                  // s1
    UnkStruct_McFatCtx *ctx;
    UnkStruct_McGeom &geom = *B3_FRAME_LOCAL(UnkStruct_McGeom, 0);   // sp+0
    int err;                    // sp+0x20
    unsigned char *clock = B3_FRAME_LOCAL(unsigned char, 0x10);   // sp+0x10 (the BCD time: +1 sec .. +7 year)
    int s0;
    int s2;
    int s4;
    int s5;
    int s6;
    int s3;
    int v;
    int x;
    int i;
    int *p;
    int ret;
    char *ent;                  // D_01D8E840
    unsigned int pagesz;
    unsigned int ppc;

    tbl = D_0049B6F0 + socket * 0x184;
    if (func_00249640(socket) == 0) {
        return 0x81010013;
    }
    ctx = &D_01D8D708[socket];
    ctx->field_894 = 0;
    ctx->field_800 = -1;
    ctx->field_804 = -1;
    ctx->field_890 = 0;
    ctx->field_808[0] = D_004BB4C8[0];
    ctx->field_808[1] = D_004BB4C8[1];
    if (func_002478D8(socket, &geom) == 0) {
        return 0x8101006F;
    }
    if (_sceMcpReadSysInfo(socket) == 0) {
        return 0x8101006F;
    }
    if (strncmp(tbl, D_004BB4D0, 0x1B) != 0) {
        // [C] no valid superblock: the 32 words at +0xD0 .. +0x14C = -1, then read the bad block list into them
        p = (int *)(tbl + 0x14C);
        s4 = 0x1F;
        do {
            s4--;
            *p = -1;
            p--;
        } while (s4 >= 0);
        if (func_00248268(socket, &geom, (int *)(tbl + 0xD0)) == 0) {
            return 0x8101006F;
        }
    }
    memset(tbl, 0, 0x28);
    for (i = 0; i < 0x1C; i++) {
        tbl[i] = D_004BB4D0[i];
    }
    strcat(tbl, D_004BB4F0);
    *(unsigned short *)(tbl + 0x28) = (unsigned short)geom.field_0;
    v = mc2_div(0x400, geom.field_0);
    *(unsigned short *)(tbl + 0x2C) = (unsigned short)geom.field_4;
    *(unsigned short *)(tbl + 0x2E) = 0xFF00;
    *(unsigned short *)(tbl + 0x2A) = (unsigned short)v;
    x = v & 0xFFFF;
    *(int *)(tbl + 0x154) = 0x400;
    *(int *)(tbl + 0x158) = 0x100;
    tbl[0x150] = 2;
    tbl[0x151] = 0x2B;
    *(int *)(tbl + 0x3C) = 0;
    *(int *)(tbl + 0x48) = 0;
    *(int *)(tbl + 0x4C) = 0;
    *(int *)(tbl + 0x170) = 0;
    *(int *)(tbl + 0x30) = mc2_div(geom.field_8, x);
    *(int *)(tbl + 0x180) = -1;
    *(int *)(tbl + 0x164) = 0;
    *(int *)(tbl + 0x168) = 0;
    *(int *)(tbl + 0x16C) = 0;
    *(int *)(tbl + 0x174) = -1;
    *(int *)(tbl + 0x178) = -1;
    *(int *)(tbl + 0x17C) = -1;
    *(unsigned int *)(tbl + 0x15C) = mc2_divu(*(unsigned short *)(tbl + 0x2C), (unsigned int)x);
    *(int *)(tbl + 0x40) = mc2_div(geom.field_8, geom.field_4) - 1;
    // [C] +0x40, +0x44: the last two usable clusters, searched downward from the end
    while (func_002481B8(socket, *(int *)(tbl + 0x40)) != 0) {
        *(int *)(tbl + 0x40) = *(int *)(tbl + 0x40) - 1;
    }
    *(int *)(tbl + 0x44) = *(int *)(tbl + 0x40) - 1;
    s2 = func_002481B8(socket, *(int *)(tbl + 0x44));
    while (s2 != 0) {
        *(int *)(tbl + 0x44) = *(int *)(tbl + 0x44) - 1;
        s2 = func_002481B8(socket, *(int *)(tbl + 0x44));
    }
    // s6: FAT pages needed (4 bytes per cluster, 1024 bytes per page); s5: indirect pages needed (at most 0x20)
    x = *(int *)(tbl + 0x30) << 2;
    v = x - 1;
    if (v < 0) {
        v = x + 0x3FE;
    }
    s6 = (v >> 10) + 1;
    x = s6 << 2;
    v = x - 1;
    if (v < 0) {
        v = x + 0x3FE;
    }
    s5 = (v >> 10) + 1;
    x = geom.field_4 * geom.field_0;
    if (x < 0) {
        x = x + 0x3FF;
    }
    s0 = x >> 10;
    if (s5 >= 0x21) {
        s5 = 0x20;
        s6 = 0x2000;
    }
    p = (int *)(tbl + 0x50);
    memset(p, 0, 0x80);
    s4 = 0;
    if (s2 < s5) {
        for (;;) {
            // the next usable cluster for the indirect page list
            do {
                s2 = func_00248210(socket, s0);
                if (s2 == 1) {
                    s0 = s0 + 1;
                }
            } while (s2 != 0);
            s4++;
            *p = s0;
            s0++;
            if (!(s4 < s5)) {
                break;
            }
            p++;
        }
    }
    s2 = 0;
    for (i = 0; i < 0x100; i++) {
        ctx->field_0[i] = (unsigned int)-1;
    }
    for (i = 0; i < 0x100; i++) {
        ctx->field_400[i] = (unsigned int)-1;
    }
    s4 = s6 - 1;
    if (s4 >= 0) {
        int *ifc;

        ifc = (int *)(tbl + 0x4C);
        do {
            while (func_00248210(socket, s0) != 0) {
                s0++;
            }
            if (func_00247BB8(socket, ctx->field_400, s0, 1) == 0) {
                return 0x8101006F;
            }
            ctx->field_0[s2] = s0;
            s2++;
            if (s2 >= 0x100 || s4 == 0) {
                ifc++;
                if (func_00247BB8(socket, ctx->field_0, *ifc, 1) == 0) {
                    return 0x8101006F;
                }
                for (i = 0; i < 0x100; i++) {
                    ctx->field_0[i] = (unsigned int)-1;
                }
                s2 = 0;
            }
            s4--;
            s0++;
        } while (s4 >= 0);
    }
    *(int *)(tbl + 0x34) = s0;
    s2 = 0;
    v = mc2_div(*(int *)(tbl + 0x30), 0x3E8);
    s3 = v * 0x3E8 + 1;
    x = mc2_div(*(int *)(tbl + 0x44) * *(unsigned short *)(tbl + 0x2C), *(unsigned short *)(tbl + 0x2A));
    x = x - s0;
    *(int *)(tbl + 0x38) = x;
    s4 = 0;
    if (x > 0) {
        do {
            if (s2 == s3) {
                *(int *)(tbl + 0x170) = s0 - *(int *)(tbl + 0x34);
            }
            v = func_00248210(socket, s0);
            if (v == 1) {
                ret = _sceMcFatWriteFatData(socket, s4, (unsigned int)-3, &err);
                if (err != 0) {
                    return ret;
                }
            } else {
                if (s2 == 0) {
                    *(int *)(tbl + 0x34) = s0;
                }
                _sceMcFatWriteFatData(socket, s4, 0x7FFFFFFF, &err);
                if (err != 0) {
                    return err;
                }
                s2++;
            }
            s4++;
            s0++;
        } while (s4 < *(int *)(tbl + 0x38));
    }
    x = *(int *)(tbl + 0x44) * *(unsigned short *)(tbl + 0x2C);
    *(int *)(tbl + 0x38) = mc2_div(x, *(unsigned short *)(tbl + 0x2A)) - *(int *)(tbl + 0x34);
    s0 = _sceMcFatGetFreeClust(socket, &err);
    if (err != 0) {
        return err;
    }
    if (s0 != 0) {
        scePrintf(D_004BB500, s0);
        return 0x81019001;
    }
    // root directory: the "." and ".." entries at cluster 0, time = the card clock (BCD)
    ent = D_01D8E840;
    _sceMcCoreReadClock(socket, clock);
    memset(ent, 0, 0x200);
    *(unsigned short *)(ent + 0x00) = 0x8427;
    *(unsigned short *)(ent + 0x0E) = (unsigned short)(mc2_bcd(clock[7]) + 0x7D0);
    ent[0x0D] = (char)mc2_bcd(clock[6]);
    ent[0x0C] = (char)mc2_bcd(clock[5]);
    ent[0x0B] = (char)mc2_bcd(clock[3]);
    ent[0x0A] = (char)mc2_bcd(clock[2]);
    ent[0x09] = (char)mc2_bcd(clock[1]);
    *(unsigned short *)(ent + 0x02) = 0;
    ent[0x08] = 0;
    for (i = 0; i < 8; i++) {
        ent[0x18 + i] = ent[0x08 + i];
    }
    *(int *)(ent + 0x04) = 2;
    *(unsigned short *)(ent + 0x40) = *(unsigned short *)D_004BB4B0;
    *(int *)(ent + 0x10) = 0;
    *(int *)(ent + 0x14) = 0;
    *(int *)(ent + 0x20) = 0;
    memset(ent + 0x200, 0, 0x200);
    ent += 0x200;
    *(unsigned short *)(ent + 0x00) = 0xA426;
    *(unsigned short *)(ent + 0x0E) = (unsigned short)(mc2_bcd(clock[7]) + 0x7D0);
    ent[0x0D] = (char)mc2_bcd(clock[6]);
    ent[0x0C] = (char)mc2_bcd(clock[5]);
    ent[0x0B] = (char)mc2_bcd(clock[3]);
    ent[0x0A] = (char)mc2_bcd(clock[2]);
    ent[0x09] = (char)mc2_bcd(clock[1]);
    *(unsigned short *)(ent + 0x02) = 0;
    ent[0x08] = 0;
    *(int *)(ent + 0x04) = 0;
    for (i = 0; i < 8; i++) {
        ent[0x18 + i] = ent[0x08 + i];
    }
    *(int *)(ent + 0x10) = 0;
    *(int *)(ent + 0x14) = 0;
    *(int *)(ent + 0x20) = 0;
    *(unsigned short *)(ent + 0x40) = *(unsigned short *)D_004BB4B8;
    ent[0x42] = D_004BB4B8[2];
    if (func_002481A0(socket, D_01D8E840, 0, 1) == 0) {
        return 0x8101006F;
    }
    _sceMcFatWriteFatData(socket, 0, (unsigned int)-1, &err);
    if (err != 0) {
        return err;
    }
    if (func_00247C00(socket) == 0) {
        return 0x8101006F;
    }
    if (_sceMcFatFlushFatCache(socket) == 0) {
        return 0x8101006F;
    }
    if (_sceMcpWriteSysInfo(socket) == 0) {
        return 0x8101006F;
    }
    D_01D8D708[socket].field_88C = 1;
    D_01D8D708[socket].field_888 = 0;
    return 0;
}

// [C] erases the whole card: needs a connection (else 0x81010013); resets the FAT context (field_88C = 0, "/"),
// reads the card spec and erases every block (page count / pages per block, _sceMcCoreBlockErase). Returns 0, or
//   0x8101006F when the spec or an erase failed.
// 0x00244F28
extern "C" int _sceMcFatUnFormat(int socket)
{
    // boot2 E1: locals passed down to calls live on the guest stack at retail's offsets
    // [C 0x244F28 addiu sp, -0x30; daddu a1, sp (func_002478D8 a1)]
    B3_RETAIL_FRAME(0x30);
    UnkStruct_McGeom &geom = *B3_FRAME_LOCAL(UnkStruct_McGeom, 0);   // sp+0
    UnkStruct_McFatCtx *ctx;
    int count;
    int i;

    if (func_00249640(socket) == 0) {
        return 0x81010013;
    }
    if (func_002478D8(socket, &geom) == 0) {
        return 0x8101006F;
    }
    ctx = &D_01D8D708[socket];
    ctx->field_894 = 0;
    ctx->field_800 = -1;
    ctx->field_804 = -1;
    ctx->field_890 = 0;
    ctx->field_88C = 0;
    ctx->field_808[0] = D_004BB4C8[0];
    ctx->field_808[1] = D_004BB4C8[1];
    count = mc2_div(geom.field_8, geom.field_4);
    i = 0;
    if (count > 0) {
        do {
            i++;
            if (_sceMcCoreBlockErase(socket, i - 1) == 0) {
                return 0x8101006F;
            }
        } while (i < count);
    }
    return 0;
}

// [C] common front of the path functions: resolves path a1 against the current directory of the socket into out
// (a2) via func_002436C0. Errors: 0x81010013 (no connection), 0x8101006F (card not valid: field_88C == 0, or the
// card-changed check failed), 0x81010016 (bad path or characters), 0x8101005B (name part too long). 0 when fine.
// 0x00245040
extern "C" int func_00245040(int socket, char *path, char *out)
{
    int v;

    if (func_00249640(socket) == 0) {
        return 0x81010013;
    }
    if (D_01D8D708[socket].field_88C == 0) {
        return 0x8101006F;
    }
    if (func_002436C0(socket, path, out) == 0 || isCorrFname(out) == 0) {
        return 0x81010016;
    }
    if (isCorrNameSize(out) == 0) {
        return 0x8101005B;
    }
    v = func_00247908(socket);
    return (v != 0) ? 0 : 0x8101006F;
}

// [C] copies one directory entry (0x200 bytes), the asm's unrolled doubleword copy.
static inline void mc2_copy_ent(char *dst, const char *src)
{
    int i;

    for (i = 0; i < 0x200; i++) {
        dst[i] = src[i];
    }
}

// [C] searches the directory whose chain starts at clust (count entries) for an in-use entry (bit 0x8000 of the
// attribute) named name. The first cluster must start with "." and "..", else 0x81019002. Copies the found entry to
// out (if not 0), its index to *posOut (if not 0) and returns 0. Errors: 0x8101006F (cluster read), chain errors,
//   0x81010002 (not found: chain ended).
// 0x00245118
extern "C" int _SearchEntry(int socket, char *out, int *posOut, const char *name, int clust, int count)
{
    // boot2 E1: locals passed down to calls live on the guest stack at retail's offsets
    // [C 0x245118 addiu sp, -0x460; daddu a1, sp (func_00248188 a1); err sw zero, 0x400(sp)]
    B3_RETAIL_FRAME(0x460);
    char *buf = B3_FRAME_LOCAL(char, 0);   // sp+0, 0x400 bytes
    int err;                    // sp+0x400
    int pos;                    // s4
    int first;                  // s7
    char *ent;
    int n;
    int next;
    int dec;
    unsigned int attr;

    err = 0;
    pos = 0;
    first = 1;
    for (;;) {
        if (func_00248188(socket, buf, clust, 1) == 0) {
            return 0x8101006F;
        }
        if (first != 0) {
            first = 0;
            if (strlen(buf + 0x40) >= 0x20) {
                return 0x81019002;
            }
            if (strcmp(buf + 0x40, D_004BB4B0) != 0) {
                return 0x81019002;
            }
            if (strlen(buf + 0x240) >= 0x20) {
                return 0x81019002;
            }
            if (strcmp(buf + 0x240, D_004BB4B8) != 0) {
                return 0x81019002;
            }
        }
        ent = buf;
        n = 0;
        if (count != 0) {
            attr = *(unsigned short *)ent;
            for (;;) {
                if ((attr & 0x8000) != 0) {
                    ent[0x5F] = 0;
                    if (strcmp(name, ent + 0x40) == 0) {
                        if (out != 0) {
                            mc2_copy_ent(out, ent);
                        }
                        if (posOut != 0) {
                            *posOut = pos;
                        }
                        return 0;
                    }
                }
                dec = (count > 0);
                n++;
                count = count - dec;
                ent += 0x200;
                pos++;
                if (!(n < 2)) {
                    break;
                }
                if (count == 0) {
                    break;
                }
                attr = *(unsigned short *)ent;
            }
        }
        next = _sceMcFatGetNextClust(socket, clust, &err);
        clust = next;
        if (err != 0) {
            return err;
        }
        if (next == -1) {
            return 0x81010002;
        }
        clust = next & 0x7FFFFFFF;
    }
}

// [C] looks a path up from the root: resolves name (func_00245040), reads the root entry (must be "."), then walks the
// '/'-separated components with _SearchEntry. Entries with attribute bit 0x2000 give 0x8101000D. Results: the last
// component found -> the entry copied to ent (no null test), *a4 = its position in the parent, *a3 = the parent directory's cluster, and the return
// value is 0x81010011 (the name exists); last component missing -> the parent's entry is copied to ent and 0x81010002
// is returned; a missing inner component -> 0x81019FFF. For the path "/" the root entry is copied and 0x81010011
// returned. Other errors are passed on.
// 0x00245398
extern "C" int _sceMcFatSearchFile(int socket, char *name, UnkStruct_McDirEnt *ent, int *a3, int *a4)
{
    // boot2 E1: locals passed down to calls live on the guest stack at retail's offsets
    // [C 0x245398 addiu sp, -0x500; daddu a1, sp (_GetEntryByPos / _SearchEntry a1);
    // addiu s2, sp, 0x400 (strcpy a0, _SearchEntry a3); addiu s1, sp, 0x420 (func_00245040 a2)]
    B3_RETAIL_FRAME(0x500);
    char *tbl;
    char *ents = B3_FRAME_LOCAL(char, 0);   // sp+0 (current entry), sp+0x200 (the parent entry kept)
    // sp+0x400: the component, 0x20 bytes as in retail; a longer inner component runs on into 'path' as retail's does
    // (harmless: 'path' is not read after the first _strtok, which works on its copy D_01D8D608)
    char *comp = B3_FRAME_LOCAL(char, 0x400);
    int pos;                    // sp+0x4A0
    char *path = B3_FRAME_LOCAL(char, 0x420);   // sp+0x420, 0x80 bytes
    int e;
    int last;                   // s6
    char *tok;
    char *s0;
    int r;

    tbl = D_0049B6F0 + socket * 0x184;
    e = func_00245040(socket, name, path);
    if (e != 0) {
        return e;
    }
    last = 0;
    e = _GetEntryByPos(socket, ents, *(int *)(tbl + 0x3C), 0);
    if (e != 0) {
        return e;
    }
    if (strlen(ents + 0x40) >= 0x20) {
        return 0x81019002;
    }
    if (strcmp(ents + 0x40, D_004BB4B0) != 0) {
        return 0x81019002;
    }
    if (strcmp(path, D_004BB4C8) == 0) {
        mc2_copy_ent((char *)ent, ents);
        return 0x81010011;
    }
    tok = _strtok(path + 1, D_004BB4C8);
    for (;;) {
        strcpy(comp, tok);
        tok = _strtok(0, D_004BB4C8);
        if (tok == 0) {
            last = 1;
        }
        mc2_copy_ent(ents + 0x200, ents);
        r = _SearchEntry(socket, ents, &pos, comp, *(int *)(ents + 0x210), *(int *)(ents + 0x204));
        if ((*(unsigned short *)ents & 0x2000) != 0) {
            return 0x8101000D;
        }
        if (last == 1) {
            if (r != 0) {
                if (r != 0x81010002) {
                    return r;
                }
                mc2_copy_ent((char *)ent, ents + 0x200);
                return 0x81010002;
            }
            if (ent != 0) {
                mc2_copy_ent((char *)ent, ents);
            }
            if (a4 != 0) {
                *a4 = pos;
            }
            if (a3 != 0) {
                *a3 = *(int *)(ents + 0x210);
            }
            return 0x81010011;
        }
        if (r != 0) {
            return (r == 0x81010002) ? 0x81019FFF : r;
        }
    }
}

// [C] writes the 0x200-byte directory entry ent as entry number pos of the directory chain starting at clust: walks
// pos / 2 clusters, reads the cluster (func_00248188), replaces the entry (pos & 1 within it) and writes it back
// (func_002481A0). Returns 0, 0x8101006F (read failed, chain ended before pos, or the write failed) or a chain error.
// 0x002457E0
extern "C" int _SetEntryByPos(int socket, char *ent, int clust, int pos)
{
    int err;
    int next;

    err = 0;
    for (;;) {
        if (pos < 2) {
            if (func_00248188(socket, D_01D8E840, clust, 1) == 0) {
                return 0x8101006F;
            }
            mc2_copy_ent(D_01D8E840 + (pos << 9), ent);
            return (func_002481A0(socket, D_01D8E840, clust, 1) != 0) ? 0 : 0x8101006F;
        }
        next = _sceMcFatGetNextClust(socket, clust, &err);
        clust = next;
        if (err != 0) {
            return err;
        }
        if (next == -1) {
            return 0x8101006F;
        }
        clust = next & 0x7FFFFFFF;
        pos = pos - 2;
    }
}

// [C] counts the free entries (attribute bit 0x8000 clear) of a directory: from cluster clust, count entries in all.
// Returns the number (+1 when the scan ended on the first entry of a cluster, as the asm does), or -1 with *err set
// (0x8101006F cluster read failed, or a chain error).
// 0x002459A0
extern "C" int _GetFreeEntrySize(int socket, int clust, int count, int *err)
{
    int free;       // s2
    int idx;        // a1: entry index in the cluster
    char *ent;
    unsigned int attr;
    int next;

    free = 0;
    *err = 0;
    for (;;) {
        if (func_00248188(socket, D_01D8E840, clust, 1) == 0) {
            *err = 0x8101006F;
            return -1;
        }
        idx = 0;
        ent = D_01D8E840;
        attr = *(unsigned short *)ent;
        for (;;) {
            ent += 0x200;
            count--;
            if ((attr & 0x8000) == 0) {
                free = free + 1;
            }
            if (count == 0) {
                return (idx != 0) ? free : free + 1;
            }
            idx++;
            if (!(idx < 2)) {
                break;
            }
            attr = *(unsigned short *)ent;
        }
        next = _sceMcFatGetNextClust(socket, clust, err);
        clust = next;
        if (*err != 0) {
            return -1;
        }
        if (next == -1) {
            return free;
        }
        clust = next & 0x7FFFFFFF;
    }
}

// [C] adds the 0x200-byte entry newEnt to the directory (chain from clust, count entries). A free slot (or the slot
// at the end, when count is 0) takes it; when the directory's clusters are full a cluster is allocated and chained. For
// a count of 0 the directory's own length (+4 of the parent's entry, located through the "." entry: +0x10 cluster,
// +0x14 position) is incremented. Returns the entry index, or -1 with *err set (0x8101006F, 0x81019002, 0x81010017
// when more than 0x14 entries are passed in a non-root directory, chain / FAT errors); when the parent link is
// missing after growth it returns 0x81019002 itself.
// 0x00245AC8
extern "C" int _CreateEntry(int socket, char *newEnt, int clust, int count, int *err)
{
    // boot2 E1: locals passed down to calls live on the guest stack at retail's offsets
    // [C 0x245AC8 addiu sp, -0x460; daddu a1, sp (_GetEntryByPos / _SetEntryByPos a1);
    // sw v0, 0x210(sp) (dot +0x10 = -1); addiu a0, sp, 0x200]
    B3_RETAIL_FRAME(0x460);
    char *pbuf = B3_FRAME_LOCAL(char, 0);   // sp+0: the parent's entry
    char *dot = B3_FRAME_LOCAL(char, 0x200);   // sp+0x200: copy of this directory's "." entry
    int rootflag;               // sp+0x400
    int fail;                   // sp+0x404
    int fp;
    int s4;
    int more;
    int dec;
    char *s0;
    unsigned int attr;
    int next;
    int old;
    int e;

    *(int *)(dot + 0x10) = -1;
    rootflag = (clust == 0);
    fail = 0x8101006F;
    fp = 0;
    for (;;) {
        if (func_00248188(socket, D_01D8E840, clust, 1) == 0) {
            *err = fail;
            return -1;
        }
        s4 = 0;
        s0 = D_01D8E840;
        for (;;) {
            attr = *(unsigned short *)s0;
            if ((attr & 1) != 0) {
                if (strcmp(s0 + 0x40, D_004BB4B0) == 0) {
                    mc2_copy_ent(dot, s0);
                }
                attr = *(unsigned short *)s0;
            }
            if ((attr & 0x8000) != 0 && count != 0) {
                s4++;
                dec = (count > 0);
                more = (s4 < 2);
                count = count - dec;
                s0 += 0x200;
                fp++;
                if (more) {
                    continue;
                }
                break;
            }
            // a free slot, or the end of the used entries
            mc2_copy_ent(s0, newEnt);
            if (func_002481A0(socket, D_01D8E840, clust, 1) == 0) {
                *err = fail;
                return -1;
            }
            if (count != 0) {
                *err = 0;
                return fp;
            }
            if (*(int *)(dot + 0x10) == -1) {
                *err = 0x81019002;
                return -1;
            }
            goto update_parent;
        }
        old = clust;
        if (rootflag == 0) {
            if (!(fp < 0x14)) {
                *err = 0x81010017;
                return -1;
            }
        }
        next = _sceMcFatGetNextClust(socket, old, err);
        clust = next;
        if (*err != 0) {
            return -1;
        }
        if (next != -1) {
            clust = next & 0x7FFFFFFF;
            continue;
        }
        clust = _sceMcFatGetFreeClust(socket, err);
        if (*err != 0) {
            return -1;
        }
        _sceMcFatWriteFatData(socket, old, (unsigned int)clust | 0x80000000u, err);
        if (*err != 0) {
            return -1;
        }
        _sceMcFatWriteFatData(socket, clust, (unsigned int)-1, err);
        if (*err != 0) {
            return -1;
        }
        mc2_copy_ent(D_01D8E840, newEnt);
        *(unsigned short *)(D_01D8E840 + 0x200) = 0;
        if (func_002481A0(socket, D_01D8E840, clust, 1) == 0) {
            *err = fail;
            return -1;
        }
        if (*(int *)(dot + 0x10) == -1) {
            return 0x81019002;
        }
        goto update_parent;
    }
update_parent:
    e = _GetEntryByPos(socket, pbuf, *(int *)(dot + 0x10), *(int *)(dot + 0x14));
    if (e != 0) {
        *err = e;
        return -1;
    }
    *(int *)(pbuf + 4) = *(int *)(pbuf + 4) + 1;
    e = _SetEntryByPos(socket, pbuf, *(int *)(dot + 0x10), *(int *)(dot + 0x14));
    if (e == 0) {
        *err = 0;
        return fp;
    }
    *err = e;
    return -1;
}

// [C] fills the time fields of a directory entry from the card clock (BCD bytes clock[1..7], index 4 unused):
// +9 sec? ... in asm order: +9 = clock[1], +0xA = clock[2], +0xB = clock[3], +0xC = clock[5], +0xD = clock[6],
// +0xE (u16) = year = bcd(clock[7]) + 0x7D0.
static inline void mc2_set_time(char *ent, const unsigned char *clock)
{
    *(unsigned short *)(ent + 0x0E) = (unsigned short)(mc2_bcd(clock[7]) + 0x7D0);
    ent[0x0D] = (char)mc2_bcd(clock[6]);
    ent[0x0C] = (char)mc2_bcd(clock[5]);
    ent[0x0B] = (char)mc2_bcd(clock[3]);
    ent[0x0A] = (char)mc2_bcd(clock[2]);
    ent[0x09] = (char)mc2_bcd(clock[1]);
}

// [C] creates a file or directory entry (mode a2, 0 = 0x8417; bit 0x20 = directory, only directly below the root, else
//   0x81019004): resolves the name (func_00245040), the name must not exist (_sceMcFatSearchFile returning 0x81010002 and
// the parent entry), checks the space (free clusters + a free slot against 1 or 2 needed, else 0x8101001C), allocates
// and terminates a cluster for a directory, builds the entry (time from the card clock, name = 32 bytes after the last
// '/'), adds it (_CreateEntry), for a directory writes its first cluster with "." and ".." entries, flushes the FAT
// cache and the user cluster cache. Returns 0 or the error code.
// 0x00245FC0
extern "C" int func_00245FC0(int socket, char *name, unsigned int a2)
{
    // boot2 E1: locals passed down to calls live on the guest stack at retail's offsets
    // [C 0x245FC0 addiu sp, -0x4F0; daddu a2, sp (_sceMcFatSearchFile a2); addiu v0, sp, 0x200 / sh fp, 0x200(sp);
    // addiu s5, sp, 0x400 (path); addiu a1, sp, 0x480 (_sceMcCoreReadClock a1)]
    B3_RETAIL_FRAME(0x4F0);
    char *pent = B3_FRAME_LOCAL(char, 0);   // sp+0: the parent directory's entry
    char *nent = B3_FRAME_LOCAL(char, 0x200);   // sp+0x200: the new entry
    char *path = B3_FRAME_LOCAL(char, 0x400);   // sp+0x400, 0x80 bytes
    unsigned char *clock = B3_FRAME_LOCAL(unsigned char, 0x480);   // sp+0x480, 8 bytes
    int err;                    // sp+0x490
    unsigned int mode;          // fp
    int isdir;                  // s6
    int nest;
    int top;                    // s0: the entry is directly below the root
    int e;
    int freeclust;              // s0
    int freeent;                // s1
    int need;
    int newclust;               // s2
    int index;                  // fp after CreateEntry
    char *p;
    char *ent;
    int i;

    e = func_00245040(socket, name, path);
    err = e;
    if (e != 0) {
        return e;
    }
    mode = a2;
    if (mode == 0) {
        mode = 0x8417;
    }
    isdir = (mode & 0x20);
    if (isdir != 0) {
        if (!(_get_dir_nest(path) < 2)) {
            return 0x81019004;
        }
    }
    nest = _get_dir_nest(path);
    top = ((nest ^ 1) == 0);
    e = _sceMcFatSearchFile(socket, path, (UnkStruct_McDirEnt *)pent, 0, 0);
    err = e;
    if (e != 0x81010002) {
        return e;
    }
    if (top == 0) {
        // [G] the asm subtracts the caller's s1 register here (basic_thread keeps 0x01D80000 = %hi(D_01D810C0) in it):
        // the test is "parent length - 0x01D80000 < 0x14", always true for a real length
        if (!(*(int *)(pent + 4) - 0x01D80000 < 0x14)) {
            return 0x81010017;
        }
    }
    freeclust = _sceMcFatGetFreeClustSize(socket, &err);
    if (err != 0) {
        return err;
    }
    freeent = _GetFreeEntrySize(socket, *(int *)(pent + 0x10), *(int *)(pent + 4), &err);
    if (err != 0) {
        return err;
    }
    freeclust = freeclust + (err < freeent);
    need = (isdir != 0) ? 2 : 1;
    if (freeclust < need) {
        return 0x8101001C;
    }
    newclust = 0;
    if (isdir != 0) {
        newclust = _sceMcFatGetFreeClust(socket, &err);
        if (err != 0) {
            return err;
        }
        _sceMcFatWriteFatData(socket, newclust, (unsigned int)-1, &err);
        if (err != 0) {
            return err;
        }
    }
    _sceMcCoreReadClock(socket, clock);
    memset(nent, 0, 0x200);
    *(unsigned short *)(nent + 0x00) = (unsigned short)mode;
    mc2_set_time(nent, clock);
    *(unsigned short *)(nent + 0x02) = 0;
    nent[0x08] = 0;
    *(int *)(nent + 0x10) = (isdir != 0) ? newclust : -1;
    for (i = 0; i < 8; i++) {
        nent[0x18 + i] = nent[0x08 + i];
    }
    *(int *)(nent + 0x04) = (isdir != 0) ? 2 : 0;
    *(int *)(nent + 0x14) = 0;
    p = strrchr(path, 0x2F);
    *(int *)(nent + 0x20) = 0;
    for (i = 0; i < 0x20; i++) {
        nent[0x40 + i] = p[1 + i];
    }
    index = _CreateEntry(socket, nent, *(int *)(pent + 0x10), *(int *)(pent + 4), &err);
    if (index < 0) {
        return err;
    }
    if (isdir != 0) {
        // the new directory's first cluster: "." (cluster 0 = the root as parent, position index) and ".."
        ent = D_01D8E840;
        memset(ent, 0, 0x200);
        *(unsigned short *)(ent + 0) = 0x8427;
        mc2_set_time(ent, clock);
        *(unsigned short *)(ent + 0x02) = 0;
        for (i = 0; i < 8; i++) {
            ent[0x18 + i] = ent[0x08 + i];
        }
        *(int *)(ent + 0x14) = index;
        *(unsigned short *)(ent + 0x40) = *(unsigned short *)D_004BB4B0;
        *(int *)(ent + 0x04) = 0;
        *(int *)(ent + 0x10) = 0;
        ent += 0x200;
        memset(ent, 0, 0x200);
        *(unsigned short *)(ent + 0) = 0x8427;
        mc2_set_time(ent, clock);
        ent[0x08] = 0;
        *(unsigned short *)(ent + 0x02) = 0;
        for (i = 0; i < 8; i++) {
            ent[0x18 + i] = ent[0x08 + i];
        }
        *(int *)(ent + 0x04) = 0;
        *(int *)(ent + 0x10) = 0;
        *(int *)(ent + 0x14) = 0;
        *(int *)(ent + 0x20) = 0;
        *(unsigned short *)(ent + 0x40) = *(unsigned short *)D_004BB4B8;
        ent[0x42] = D_004BB4B8[2];
        if (func_002481A0(socket, D_01D8E840, *(int *)(nent + 0x10), 1) == 0) {
            return 0x8101006F;
        }
    }
    if (_sceMcFatFlushFatCache(socket) == 0) {
        return 0x8101006F;
    }
    e = func_00247C00(socket);
    return (e != 0) ? err : 0x8101006F;
}

// [C] reads a file: resolves name, finds it (must exist: _sceMcFatSearchFile == 0x81010011, in-use bit, not hidden
//   0x2000 -> 0x8101000D, not a directory 0x20 -> 0x81010002), offset (a3) beyond the length -> 0x81010016; size (a4) 0 or
// too large is clamped to the rest of the file. Copies size bytes from the file offset into the buffer a2 through the
// cluster buffer D_01D8E840 (0x400 per cluster, following the FAT chain). Returns the byte count, or an error code
// (0x8101006F cluster read, 0x81019002 chain ended early, chain errors).
// [G] when the search does not return 0x81010011 the asm returns the uninitialised stack word sp+0x280 (the error
// local); modelled as 0 here.
// 0x00246588
extern "C" int func_00246588(int socket, char *name, unsigned int a2, unsigned int a3, unsigned int a4)
{
    // boot2 E1: locals passed down to calls live on the guest stack at retail's offsets
    // [C 0x246588 addiu sp, -0x2E0; addiu s0, sp, 0x200 (func_00245040 a2); daddu a2, sp (_sceMcFatSearchFile a2)]
    B3_RETAIL_FRAME(0x2E0);
    char *path = B3_FRAME_LOCAL(char, 0x200);   // sp+0x200, 0x80 bytes
    char *ent = B3_FRAME_LOCAL(char, 0);   // sp+0, 0x200 bytes
    int err;                    // sp+0x280
    char *dst;                  // s4
    int offset;                 // s1
    int size;                   // s3
    int rest;
    int ret;                    // fp
    int clust;                  // s2
    int n;
    unsigned int attr;
    int e;
    int i;

    err = 0;
    e = func_00245040(socket, name, path);
    if (e != 0) {
        return e;
    }
    e = _sceMcFatSearchFile(socket, path, (UnkStruct_McDirEnt *)ent, 0, 0);
    if (e != 0x81010011) {
        return err;
    }
    attr = *(unsigned short *)ent;
    if ((attr & 1) == 0) {
        return 0x8101000D;
    }
    if ((attr & 0x2000) != 0) {
        return 0x8101000D;
    }
    if ((attr & 0x20) != 0) {
        return 0x81010002;
    }
    offset = (int)a3;
    size = (int)a4;
    if (*(int *)(ent + 4) < offset) {
        return 0x81010016;
    }
    rest = *(int *)(ent + 4) - offset;
    dst = (char *)a2;
    if (size == 0) {
        size = rest;
    }
    if (*(int *)(ent + 4) < size + offset) {
        size = rest;
    }
    ret = size;
    if (ret == 0) {
        return ret;
    }
    clust = *(int *)(ent + 0x10);
    do {
        if (offset < 0x400) {
            err = func_00248188(socket, D_01D8E840, clust, 1);
            if (err == 0) {
                return 0x8101006F;
            }
            n = 0x400 - offset;
            if (n < size) {
                for (i = 0; i < n; i++) {
                    dst[i] = D_01D8E840[offset + i];
                }
                dst += n;
                size = size + offset - 0x400;
                offset = 0;
            } else {
                for (i = 0; i < size; i++) {
                    dst[i] = D_01D8E840[offset + i];
                }
                return ret;
            }
        }
        e = _sceMcFatGetNextClust(socket, clust, &err);
        clust = e;
        if (offset != 0) {
            offset = offset - 0x400;
        }
        if (err != 0) {
            return err;
        }
        if (e == -1) {
            return 0x81019002;
        }
        clust = e & 0x7FFFFFFF;
    } while (size != 0);
    return ret;
}

// [C] writes a file (name, buffer a2, file offset a3, size a4): the file must exist (_sceMcFatSearchFile ==
//   0x81010011), be a plain file (type bits 0x8000 / 0xA000, bits 0xF00 0 or 0x400, name terminator byte +0x60 zero ->
// else 0x81019002), writable (bit 2, else 0x8101000D), not a directory (0x20 -> 0x81010002), offset within the length
// (0x81010016). Extends the cluster chain as needed (free space checked against the chain length, 0x8101001C), creates
// the first cluster of an empty file, copies the data through the cluster buffer D_01D8E840 (a partial cluster is read
// first when the write position lies inside the file), flushes, sets the modification time, the 0x80 attribute bit and
// the length (max of old length and the end position) and writes the directory entry. Returns the size written, or an
// error code (0x8101006F ...).
// [G] the asm reads the never-initialised stack word sp+0x2A8 when it creates the first cluster of an empty file and
// raises the entry's length to it (the entry is rewritten with the real length at the end); modelled as 0.
// 0x00246788
extern "C" int func_00246788(int socket, char *name, unsigned int a2, unsigned int a3, unsigned int a4)
{
    // boot2 E1: locals passed down to calls live on the guest stack at retail's offsets
    // [C 0x246788 addiu sp, -0x300; addiu s0, sp, 0x200 (func_00245040 a2); daddu a2, sp (_sceMcFatSearchFile a2);
    // addiu a1, sp, 0x280 (_sceMcCoreReadClock a1)]
    B3_RETAIL_FRAME(0x300);
    char *path = B3_FRAME_LOCAL(char, 0x200);   // sp+0x200, 0x80 bytes
    char *ent = B3_FRAME_LOCAL(char, 0);   // sp+0, 0x200 bytes
    unsigned char *clock = B3_FRAME_LOCAL(unsigned char, 0x280);   // sp+0x280, 8 bytes
    int cl;                     // sp+0x290: the parent directory cluster of the entry
    int pos;                    // sp+0x294: the entry's position in it
    int err;                    // sp+0x298
    int err2;                   // sp+0x29C
    char *buf;                  // s3
    int offset;                 // s6
    int size;                   // fp
    int origsize;               // sp+0x2A4
    int curpos;                 // sp+0x2A8
    int need;                   // s1
    int have;                   // s0
    int clust;                  // s1 (later)
    int prev;                   // s0 (later)
    int e;
    int r;
    int s2;
    int n;
    int i;
    unsigned int attr;
    unsigned int t;

    buf = (char *)a2;
    offset = (int)a3;
    size = (int)a4;
    curpos = 0;     // [G] uninitialised in the asm
    e = func_00245040(socket, name, path);
    err = e;
    if (e != 0) {
        return e;
    }
    e = _sceMcFatSearchFile(socket, path, (UnkStruct_McDirEnt *)ent, &cl, &pos);
    err = e;
    if (e != 0x81010011) {
        return e;
    }
    if (ent[0x60] != 0) {
        return 0x81019002;
    }
    attr = *(unsigned short *)ent;
    t = attr & 0xF000;
    if (t != 0x8000 && t != 0xA000) {
        return 0x81019002;
    }
    t = attr & 0xF00;
    if (t != 0x400 && t != 0) {
        return 0x81019002;
    }
    if ((attr & 2) == 0) {
        return 0x8101000D;
    }
    if ((attr & 0x20) != 0) {
        return 0x81010002;
    }
    if (*(int *)(ent + 4) < offset) {
        return 0x81010016;
    }
    n = offset + size + 0x3FF;
    if (n < 0) {
        n = offset + size + 0x7FE;
    }
    need = n >> 10;
    have = func_00244118(socket, *(int *)(ent + 0x10));
    if (have < 0) {
        return have < need;     // [C] the asm returns the slt result here (likely delay slot of bltzl)
    }
    if (have != 0) {
        n = _sceMcFatGetFreeClustSize(socket, &err);
        if (err != 0) {
            return err;
        }
        if (n < need - have) {
            return 0x8101001C;
        }
    }
    if (*(int *)(ent + 0x10) == -1) {
        // empty file: its first cluster
        clust = _sceMcFatGetFreeClust(socket, &err);
        if (err != 0) {
            return err;
        }
        _sceMcFatWriteFatData(socket, clust, (unsigned int)-1, &err);
        if (err != 0) {
            return err;
        }
        _sceMcCoreReadClock(socket, clock);
        *(int *)(ent + 0x10) = clust;
        mc2_set_time(ent + 0x10, clock);
        if (*(int *)(ent + 4) < curpos) {
            *(int *)(ent + 4) = curpos;
        }
        r = _SetEntryByPos(socket, ent, cl, pos);
        if (r != 0) {
            err = r;
            return r;
        }
        err = r;
        err2 = _sceMcFatFlushFatCache(socket);
        if (err2 == 0) {
            return 0x8101006F;
        }
        err2 = func_00247C00(socket);
        if (err2 == 0) {
            return 0x8101006F;
        }
    }
    origsize = size;
    clust = *(int *)(ent + 0x10);
    curpos = offset;
    for (;;) {
        prev = clust;
        if (offset < 0x400) {
            s2 = offset + size;
            n = 0x400;
            if (s2 < 0x400) {
                if (curpos < *(int *)(ent + 4)) {
                    err2 = func_00248188(socket, D_01D8E840, clust, 1);
                    if (err2 == 0) {
                        return 0x8101006F;
                    }
                }
            }
            have = 0x400 - offset;
            if (have < size) {
                for (i = 0; i < have; i++) {
                    D_01D8E840[offset + i] = buf[i];
                }
                err2 = func_002481A0(socket, D_01D8E840, clust, 1);
                if (err2 == 0) {
                    return 0x8101006F;
                }
                buf += have;
                size = s2 - 0x400;
                curpos = curpos - offset + 0x400;
                offset = 0;
            } else {
                for (i = 0; i < size; i++) {
                    D_01D8E840[offset + i] = buf[i];
                }
                err2 = func_002481A0(socket, D_01D8E840, clust, 1);
                if (err2 == 0) {
                    return 0x8101006F;
                }
                // finish: flush, time, length, entry
                curpos = curpos + size;
                err2 = _sceMcFatFlushFatCache(socket);
                if (err2 == 0) {
                    return 0x8101006F;
                }
                err2 = func_00247C00(socket);
                if (err2 == 0) {
                    return 0x8101006F;
                }
                _sceMcCoreReadClock(socket, clock);
                mc2_set_time(ent + 0x10, clock);
                if (*(int *)(ent + 4) < curpos) {
                    *(int *)(ent + 4) = curpos;
                }
                *(unsigned short *)ent = (unsigned short)(*(unsigned short *)ent | 0x80);
                r = _SetEntryByPos(socket, ent, cl, pos);
                err = r;
                if (r != 0) {
                    return r;
                }
                err2 = func_00247C00(socket);
                return (err2 != 0) ? origsize : 0x8101006F;
            }
            prev = clust;
        }
        clust = _sceMcFatGetNextClust(socket, prev, &err2);
        if (err2 != 0) {
            return err2;
        }
        if (clust == -1) {
            clust = _sceMcFatGetFreeClust(socket, &err);
            if (err != 0) {
                return err;
            }
            _sceMcFatWriteFatData(socket, clust, (unsigned int)-1, &err);
            if (err != 0) {
                return err;
            }
            _sceMcFatWriteFatData(socket, prev, (unsigned int)clust | 0x80000000u, &err);
            if (err != 0) {
                return err;
            }
        }
        if (offset != 0) {
            offset = offset - 0x400;
        }
        clust = clust & 0x7FFFFFFF;
    }
}

extern char D_004BB520[];                   // [C] 0x004BB520 (rodata) "/*"

// [C] deletes a file or directory: resolves and finds the entry (0x81010011), must be writable (bit 2, else
//   0x8101000D); a directory must be empty (_sceMcFatGetDir on "<path>/*" reporting a count > 0 gives 0x8101005A). Clears
// the in-use bit (0x8000) of the entry and writes it (_SetEntryByPos), then walks the entry's cluster chain freeing each
// FAT entry (bit 31 cleared) until -1 / -3 / a link without bit 31, flushes, and resets the free-cluster search start
// field_890. Returns 0 or the error code (0x8101006F ...).
// 0x00246DC8
extern "C" int func_00246DC8(int socket, char *name)
{
    // boot2 E1: `count`'s address goes to a guest call as an integer word: a guest-stack local at retail's offset
    // [C 0x246DC8 addiu sp, -0x350; addiu t1, sp, 0x308]
    B3_RETAIL_FRAME(0x350);
    char *path = B3_FRAME_LOCAL(char, 0x200);   // sp+0x200 [C addiu s1, sp, 0x200: func_00245040 a2]
    char *ent = B3_FRAME_LOCAL(char, 0);   // sp+0 [C daddu a2, sp: _sceMcFatSearchFile a2]
    char *dpath = B3_FRAME_LOCAL(char, 0x280);   // sp+0x280 [C addiu s0, sp, 0x280: strcpy a0, _sceMcFatGetDir a1]
    int cl;                     // sp+0x300
    int pos;                    // sp+0x304
    int &count = *B3_FRAME_LOCAL(int, 0x308);   // sp+0x308
    int err;                    // sp+0x30C
    unsigned int attr;
    int e;
    int s0;
    int s1;

    e = func_00245040(socket, name, path);
    err = e;
    if (e != 0) {
        return e;
    }
    e = _sceMcFatSearchFile(socket, path, (UnkStruct_McDirEnt *)ent, &cl, &pos);
    err = e;
    if (e != 0x81010011) {
        return e;
    }
    attr = *(unsigned short *)ent;
    if ((attr & 2) == 0) {
        return 0x8101000D;
    }
    if ((attr & 0x20) != 0) {
        strcpy(dpath, path);
        strcat(dpath, D_004BB520);
        e = _sceMcFatGetDir(socket, dpath, 0, (unsigned int)-1, 0, (unsigned int)&count);
        err = e;
        if (e != 0) {
            return e;
        }
        if (count > 0) {
            return 0x8101005A;
        }
        attr = *(unsigned short *)ent;
    }
    *(unsigned short *)ent = (unsigned short)(attr ^ 0x8000);
    e = _SetEntryByPos(socket, ent, cl, pos);
    err = e;
    if (e != 0) {
        return e;
    }
    if (func_00247C00(socket) == 0) {
        return 0x8101006F;
    }
    s0 = *(int *)(ent + 0x10);
    if (s0 == -1) {
        return 0;
    }
    s1 = s0;
    for (;;) {
        s0 = s1;
        s1 = _sceMcFatGetNextClust(socket, s0, &err);
        if (err != 0) {
            return err;
        }
        if (s1 == -3) {
            break;
        }
        if (s1 >= 0) {
            break;
        }
        _sceMcFatWriteFatData(socket, s0, (unsigned int)s1 ^ 0x80000000u, &err);
        if (err != 0) {
            return err;
        }
        if (s1 == -1) {
            break;
        }
        s1 = s1 & 0x7FFFFFFF;
    }
    if (_sceMcFatFlushFatCache(socket) == 0) {
        return 0x8101006F;
    }
    if (func_00247C00(socket) == 0) {
        return 0x8101006F;
    }
    D_01D8D708[socket].field_890 = 0;
    return 0;
}

// [C] change directory: resolves name; "/" sets the current directory (field_808) to "/", otherwise the entry must exist
// and be a directory (0x20, else 0x81010014) and the current directory becomes "<path>/". If out (a2) is not 0 the
// resolved path is copied to it. Returns 0 or the error code.
// 0x00246FC8
extern "C" int func_00246FC8(int socket, char *name, unsigned int a2)
{
    // boot2 E1: locals passed down to calls live on the guest stack at retail's offsets
    // [C 0x246FC8 addiu sp, -0x2B0; addiu s2, sp, 0x200 (func_00245040 a2); daddu a2, sp (_sceMcFatSearchFile a2)]
    B3_RETAIL_FRAME(0x2B0);
    char *path = B3_FRAME_LOCAL(char, 0x200);   // sp+0x200, 0x80 bytes
    char *ent = B3_FRAME_LOCAL(char, 0);   // sp+0, 0x200 bytes
    char *cwd;
    int e;

    e = func_00245040(socket, name, path);
    if (e != 0) {
        return e;
    }
    if (strcmp(path, D_004BB4C8) == 0) {
        strcpy(D_01D8D708[socket].field_808, path);
    } else {
        e = _sceMcFatSearchFile(socket, path, (UnkStruct_McDirEnt *)ent, 0, 0);
        if (e != 0x81010011) {
            return e;
        }
        if ((*(unsigned short *)ent & 0x20) == 0) {
            return 0x81010014;
        }
        cwd = D_01D8D708[socket].field_808;
        strcpy(cwd, path);
        strcat(cwd, D_004BB4C8);
    }
    if (a2 != 0) {
        strcpy((char *)a2, path);
    }
    return 0;
}

// [C] lists a directory: name is "<dir>/<wildcard pattern>". a2 = the first match index to deliver (entries are
// walked from index a2 + 2, after "." and ".."), a3 = the maximum count (> 0 stops there; <= 0 means count all), a4 =
// the output array of 0x38-byte records (0 = only count), a5 = where to store the count (0 = none). Each record: +0
// entry+8 (8 bytes), +8 entry+0x18 (8 bytes), +0x10 the size (entry+4), +0x14 the attribute (u16), +0x18 the name.
// Entries without the 0x8000 bit or with 0x2000 are skipped; names are matched with isWildFit. Errors: 0x81010013, 0x8101006F
// (card not valid / cluster read), 0x81010016 (bad path or "/"), 0x81010014 (not a directory), 0x81019002 (broken
// directory: no "." / ".."), chain errors.
// 0x002470D8
extern "C" int _sceMcFatGetDir(int socket, char *name, unsigned int a2, unsigned int a3, unsigned int a4,
                               unsigned int a5)
{
    // boot2 E1: locals passed down to calls live on the guest stack at retail's offsets
    // [C 0x2470D8 addiu sp, -0x5A0; daddu a2, sp (func_002436C0 a2); addiu s1, sp, 0x80;
    // addiu t0, sp, 0x100 (isWildFit a1); addiu v1, sp, 0x140; addiu a2, sp, 0x340 (_sceMcFatSearchFile a2)]
    B3_RETAIL_FRAME(0x5A0);
    char *path = B3_FRAME_LOCAL(char, 0);   // sp+0, 0x80 bytes
    char *dir = B3_FRAME_LOCAL(char, 0x80);   // sp+0x80, 0x80 bytes
    char *pat = B3_FRAME_LOCAL(char, 0x100);   // sp+0x100 (the asm leaves 0x40 bytes)
    char *ent = B3_FRAME_LOCAL(char, 0x140);   // sp+0x140 (copy of the current entry)
    char *dent = B3_FRAME_LOCAL(char, 0x340);   // sp+0x340 (the directory's own entry)
    int err;                    // sp+0x540
    int done;                   // sp+0x544
    int start;                  // s0
    int maxcount;               // s6
    unsigned char *out;         // s2
    int *countp;                // fp
    int count;                  // s4
    int clust;                  // s3
    int idx;                    // s1
    char *cur;                  // s0 (entry pointer)
    char *s7;
    char *p;
    int e;
    int i;
    unsigned int attr;

    start = (int)a2;
    maxcount = (int)a3;
    out = (unsigned char *)a4;
    countp = (int *)a5;
    done = 0;
    err = 0;
    if (func_00249640(socket) == 0) {
        return 0x81010013;
    }
    if (D_01D8D708[socket].field_88C == 0) {
        return 0x8101006F;
    }
    if (func_002436C0(socket, name, path) == 0 || strcmp(path, D_004BB4C8) == 0) {
        return 0x81010016;
    }
    strcpy(dir, path);
    p = strrchr(dir, 0x2F);
    *p = 0;
    strcpy(pat, dir + strlen(dir) + 1);
    if (strlen(dir) != 0) {
        e = _sceMcFatSearchFile(socket, dir, (UnkStruct_McDirEnt *)dent, 0, 0);
        err = e;
        if (e != 0x81010011) {
            return e;
        }
    } else {
        e = _sceMcFatSearchFile(socket, D_004BB4C8, (UnkStruct_McDirEnt *)dent, 0, 0);
        err = e;
        if (e != 0x81010011) {
            return e;
        }
    }
    if ((*(unsigned short *)dent & 0x20) == 0) {
        return 0x81010014;
    }
    e = _GetEntryByPos(socket, ent, *(int *)(dent + 0x10), 0);
    err = e;
    if (e == 0) {
        if ((*(unsigned short *)ent & 0x8000) == 0) {
            return 0x81019002;
        }
        s7 = ent + 0x40;
        if (strcmp(s7, D_004BB4B0) != 0) {
            return 0x81019002;
        }
        e = _GetEntryByPos(socket, ent, *(int *)(dent + 0x10), 1);
        err = e;
    }
    if (e != 0) {
        if (e != 0x81010002) {
            return e;
        }
        err = 0x81019002;
        return err;
    }
    if ((*(unsigned short *)ent & 0x8000) == 0) {
        return 0x81019002;
    }
    s7 = ent + 0x40;
    if (strcmp(s7, D_004BB4B8) != 0) {
        return 0x81019002;
    }
    count = 0;
    if (!(start + 2 < *(int *)(dent + 4))) {
        if (countp != 0) {
            *countp = 0;
        }
        return 0;
    }
    clust = *(int *)(dent + 0x10);
    start = start + 2;
    for (;;) {
        if (start < 2) {
            if (func_00248188(socket, D_01D8E840, clust, 1) == 0) {
                return 0x8101006F;
            }
            idx = start;
            cur = D_01D8E840 + (idx << 9);
            for (;;) {
                mc2_copy_ent(ent, cur);
                attr = *(unsigned short *)ent;
                ent[0x5F] = 0;
                if ((attr & 0x8000) != 0 && (attr & 0x2000) == 0 && isWildFit(s7, pat) != 0) {
                    if (out != 0 && maxcount > 0) {
                        for (i = 0; i < 8; i++) {
                            out[i] = (unsigned char)ent[8 + i];
                        }
                        for (i = 0; i < 8; i++) {
                            out[8 + i] = (unsigned char)ent[0x18 + i];
                        }
                        *(int *)(out + 0x10) = *(int *)(ent + 4);
                        *(unsigned short *)(out + 0x14) = (unsigned short)attr;
                        strcpy((char *)out + 0x18, s7);
                        out += 0x38;
                    }
                    count++;
                    if (maxcount > 0 && !(count < maxcount)) {
                        done = 1;
                        break;
                    }
                }
                idx++;
                if (!(idx < 2)) {
                    break;
                }
                cur += 0x200;
            }
            start = 0;
        }
        if (done == 1) {
            break;
        }
        clust = _sceMcFatGetNextClust(socket, clust, &err);
        if (err != 0) {
            return err;
        }
        if (clust == -1) {
            break;
        }
        if (start != 0) {
            start = start - 2;
        }
        if (done != 0) {
            break;
        }
        clust = clust & 0x7FFFFFFF;
    }
    if (countp != 0) {
        *countp = count;
    }
    return 0;
}

// [C] set the attribute bits of an entry: resolves the name, finds it (0x81010011) and requires bit 0x20 (else
//   0x81010014); the new attribute = (old & 0xE7F0) | (a2 & 0x180F), written with _SetEntryByPos. Returns 0 or the error.
// 0x00247580
extern "C" int func_00247580(int socket, char *name, unsigned int a2)
{
    // boot2 E1: locals passed down to calls live on the guest stack at retail's offsets
    // [C 0x247580 addiu sp, -0x2B0; daddu a2, sp (func_00245040 a2); addiu s2, sp, 0x80 (_sceMcFatSearchFile a2)]
    B3_RETAIL_FRAME(0x2B0);
    char *path = B3_FRAME_LOCAL(char, 0);   // sp+0, 0x80 bytes
    char *ent = B3_FRAME_LOCAL(char, 0x80);   // sp+0x80, 0x200 bytes
    int cl;                     // sp+0x280
    int pos;                    // sp+0x284
    unsigned int attr;
    int e;

    e = func_00245040(socket, name, path);
    if (e != 0) {
        return e;
    }
    e = _sceMcFatSearchFile(socket, path, (UnkStruct_McDirEnt *)ent, &cl, &pos);
    if (e != 0x81010011) {
        return e;
    }
    attr = *(unsigned short *)ent;
    if ((attr & 0x20) == 0) {
        return 0x81010014;
    }
    *(unsigned short *)ent = (unsigned short)((attr & 0xE7F0) | ((a2 & 0xFFFF) & 0x180F));
    e = _SetEntryByPos(socket, ent, cl, pos);
    if (e != 0) {
        return e;
    }
    return (func_00247C00(socket) != 0) ? 0 : 0x8101006F;
}

// [C] renames name to name2 (a bare name, no '/'): both names must be valid (0x81010016) and short (0x8101005B), the new
// path must not exist (0x81010002 from the search) and the old one must (0x81010011); the entry's name (+0x40) is
// replaced and the entry written. Returns 0 or the error code.
// 0x00247648
extern "C" int _sceMcFatRename(int socket, char *name, char *name2)
{
    // boot2 E1: locals passed down to calls live on the guest stack at retail's offsets
    // [C 0x247648 addiu sp, -0x340; daddu a2, sp (_sceMcFatSearchFile a2); addiu s4, sp, 0x200; addiu s3, sp, 0x280]
    B3_RETAIL_FRAME(0x340);
    char *ent = B3_FRAME_LOCAL(char, 0);   // sp+0, 0x200 bytes
    char *path = B3_FRAME_LOCAL(char, 0x200);   // sp+0x200: the resolved old path
    char *npath = B3_FRAME_LOCAL(char, 0x280);   // sp+0x280: the new path
    int cl;                     // sp+0x300
    int pos;                    // sp+0x304
    char *p;
    int e;

    if (func_00249640(socket) == 0) {
        return 0x81010013;
    }
    if (D_01D8D708[socket].field_88C == 0) {
        return 0x8101006F;
    }
    if (func_002436C0(socket, name, path) == 0 || strchr(name2, 0x2F) != 0) {
        return 0x81010016;
    }
    strcpy(npath, path);
    p = strrchr(npath, 0x2F);
    strcpy(p + 1, name2);
    if (isCorrFname(name) == 0 || isCorrFname(name2) == 0) {
        return 0x81010016;
    }
    if (isCorrNameSize(name) == 0 || isCorrNameSize(name2) == 0) {
        return 0x8101005B;
    }
    e = _sceMcFatSearchFile(socket, npath, (UnkStruct_McDirEnt *)ent, &cl, &pos);
    if (e != 0x81010002) {
        return e;
    }
    e = _sceMcFatSearchFile(socket, path, (UnkStruct_McDirEnt *)ent, &cl, &pos);
    if (e != 0x81010011) {
        return e;
    }
    strcpy(ent + 0x40, name2);
    e = _SetEntryByPos(socket, ent, cl, pos);
    if (e != 0) {
        return e;
    }
    return (func_00247C00(socket) != 0) ? 0 : 0x8101006F;
}

// [C] number of free entries of a directory: resolves name, finds it (0x81010011), requires bit 0x20 (else 0x81010014),
// then _GetFreeEntrySize over its cluster chain (entry +0x10) and length (+4). Returns that count, or the error.
// 0x00247808
extern "C" int func_00247808(int socket, char *name)
{
    // boot2 E1: locals passed down to calls live on the guest stack at retail's offsets
    // [C 0x247808 addiu sp, -0x2B0; addiu s0, sp, 0x200 (func_00245040 a2); daddu a2, sp (_sceMcFatSearchFile a2)]
    B3_RETAIL_FRAME(0x2B0);
    char *path = B3_FRAME_LOCAL(char, 0x200);   // sp+0x200, 0x80 bytes
    char *ent = B3_FRAME_LOCAL(char, 0);   // sp+0, 0x200 bytes
    int err;                    // sp+0x280
    int e;
    int n;

    e = func_00245040(socket, name, path);
    err = e;
    if (e != 0) {
        return e;
    }
    e = _sceMcFatSearchFile(socket, path, (UnkStruct_McDirEnt *)ent, 0, 0);
    err = e;
    if (e != 0x81010011) {
        return e;
    }
    if ((*(unsigned short *)ent & 0x20) == 0) {
        return 0x81010014;
    }
    n = _GetFreeEntrySize(socket, *(int *)(ent + 0x10), *(int *)(ent + 4), &err);
    return (err != 0) ? err : n;
}

extern char D_01D92C80[];                   // [C] 0x01D92C80 (.bss, 0x800): the page buffer for the sys info
extern char D_01D8EC40[];                   // [C] 0x01D8EC40 (.bss, 0x4040): 2 user cluster caches of 0x2010 bytes

// [C] initialises the 2 user cluster caches (D_01D8EC40 + socket * 0x2010): tag +0 = -1 (none), count +0xC = 0.
// Returns 1.
// 0x002478A0
extern "C" int _sceMcpInit(void)
{
    int i;

    for (i = 0; i < 2; i++) {
        *(int *)(D_01D8EC40 + i * 0x2010) = -1;
        *(int *)(D_01D8EC40 + i * 0x2010 + 0xC) = 0;
    }
    return 1;
}

// [C] tail call to _sceMcCoreGetCardSpec.
// 0x002478D8
extern "C" int func_002478D8(int socket, UnkStruct_McGeom *out)
{
    return _sceMcCoreGetCardSpec(socket, out);
}

// [C] tail call to func_00248A68.
// 0x002478F0
extern "C" int func_002478F0(int socket)
{
    return func_00248A68(socket);
}

// [C] tail call to _sceMcCoreCheckNewCard.
// 0x00247908
extern "C" int func_00247908(int socket)
{
    return _sceMcCoreCheckNewCard(socket);
}

// [C] reads page 0 of the card (_sceMcCoreReadPage2(socket, D_01D92C80, 0, 1, 0)) and, on success, copies its first 0x184
// bytes to the socket's parameter block (D_0049B6F0 + socket * 0x184). Returns 1, or 0 when the read failed.
// 0x00247920
extern "C" int _sceMcpReadSysInfo(int socket)
{
    char *tbl;
    int i;

    if (_sceMcCoreReadPage2(socket, (unsigned int)D_01D92C80, 0, 1, 0) == 0) {
        return 0;
    }
    tbl = D_0049B6F0 + socket * 0x184;
    for (i = 0; i < 0x184; i++) {
        tbl[i] = D_01D92C80[i];
    }
    return 1;
}

// [C] fills D_01D92C80 with 0xFF (0x800 bytes), copies the socket's parameter block (0x184 bytes) over its start and
// writes it as page 0 (_sceMcCoreWritePage(socket, D_01D92C80, 0, 1)). Returns 1 when the write returned nonzero.
// 0x00247A40
extern "C" int _sceMcpWriteSysInfo(int socket)
{
    char *tbl;
    int i;

    memset(D_01D92C80, 0xFF, 0x800);
    tbl = D_0049B6F0 + socket * 0x184;
    for (i = 0; i < 0x184; i++) {
        D_01D92C80[i] = tbl[i];
    }
    return _sceMcCoreWritePage(socket, (unsigned int)D_01D92C80, 0, 1) != 0;
}

// [C] reads count clusters from cluster clust: both multiplied by the pages-per-cluster halfword at +0x2A of the parameter
// block, then tail call to func_00249160 (the page read).
// 0x00247B70
extern "C" int func_00247B70(int socket, void *buf, unsigned int clust, unsigned int count)
{
    unsigned int ppc;

    ppc = *(unsigned short *)(D_0049B6F0 + socket * 0x184 + 0x2A);
    return func_00249160(socket, buf, clust * ppc, count * ppc);
}

// [C] the same for writing: tail call to _sceMcCoreWritePage with page numbers.
// 0x00247BB8
extern "C" int func_00247BB8(int socket, void *buf, unsigned int clust, unsigned int count)
{
    unsigned int ppc;

    ppc = *(unsigned short *)(D_0049B6F0 + socket * 0x184 + 0x2A);
    return _sceMcCoreWritePage(socket, (unsigned int)buf, clust * ppc, count * ppc);
}

// [C] writes the socket's user cluster cache back: record at D_01D8EC40 + socket * 0x2010 (+0 cluster tag, +0xC page
// count, +0x10 data); with a tag of -1 nothing is done (returns 1), otherwise func_00247BB8(socket, data, tag, count)
// and the tag and count are reset to -1 / 0. Returns the write's result.
// 0x00247C00
extern "C" int func_00247C00(int socket)
{
    char *rec;
    int r;

    rec = D_01D8EC40 + socket * 0x2010;
    if (*(int *)rec == -1) {
        return 1;
    }
    r = func_00247BB8(socket, rec + 0x10, *(unsigned int *)rec, *(unsigned int *)(rec + 0xC));
    *(int *)rec = -1;
    *(int *)(rec + 0xC) = 0;
    return r;
}

// ===================================================================================================================
// Part 3 of 3 (functions 87 to 127, boot2 2026-11-06): the user cluster cache (_sceMcpWriteUserClustOnCache,
// func_00248040 ...), the card core _sceMcCore* (talks to the IOP over libdbc sockets) and the sceNetcnfif memory card
// callbacks (my_open ...).
// [C] The core's per-socket record (2 of them, D_01D93488 + socket * 0x1C): +0 created, +4 the libdbc socket id, +8 / +0xC /
// +0x10 / +0x14 the card geometry (initial 0x200, 0x10, 0x4000, 0 from _sceMcCoreInit; _sceMcCoreGetCardSpec overwrites
// them from the IOP reply: [L] page size, pages per block, page count, "spec valid" flag).
// [C] Every core request: the 0x2080-byte block D_01D934C0 (header words +0 .. +0x24 below, payload from +0x28) is sent
// with sceDbcSendData3(rec.field_4, cmdno, &{0x2080}, D_01D934C0, sceMc2SifCallback) until it returns 1, the reply
// (D_01D95540, +8 = the IOP status) fetched with SyncDma (or SyncDmaForRead for the page read). Op numbers (cmdno):
// 1 read page, 2 write page, 3 card spec, 4 func_00248A68, 6 write back, 9 block erase, 0xA read clock, 0xB write same
// data, 0xC check new card.
// [C] header words as the asm writes them: +0 first page (read / write / same data) or 0, +4 page count, +8 rec+8, +0xC
// rec+0xC, +0x10 rec+0x10, +0x14 rec+0x10 / rec+0xC - 1, +0x18 that - 2, +0x1C / +0x20 0 (block erase: +0x20 = block), +0x24
// rec+0x14 (read page: the caller's a4).
// ===================================================================================================================

#include "b3hw/hw.h"

struct UnkStruct_McCoreSock {
    int field_0;
    int field_4;
    unsigned int field_8;
    unsigned int field_C;
    unsigned int field_10;
    unsigned int field_14;
    unsigned int field_18;
};

struct UnkStruct_McCoreReq {
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
    unsigned char field_28[0x2058];
};

// [C] sceNetcnfifCreateCallbackThread's parameter (0x10 bytes used)
struct UnkStruct_McNcbParam {
    int field_0;
    int (*field_4)(const char *, const char *, int, int, int *);
    int (*field_8)(int, int, const char *, unsigned int, unsigned int, unsigned int);
    int (*field_C)(void);
};

// [C] the 0x28-byte state of the netcnfif memory card callbacks (D_01D99740; D_01D99760 is its +0x20)
struct UnkStruct_McNetSock {
    int field_0;
    int field_4;
    int field_8;
    int field_C;
    unsigned char pad_10[0x10];
    int field_20;
    int field_24;
};

extern int D_0049B9F8;                       // [C] 0x0049B9F8 (.data): SyncDma's progress word
extern unsigned int cmdno;                   // [C] 0x0049B9FC (.data): the current op number
extern char D_0049B7C0[];                    // [C] 0x0049B7C0 (.data, = D_0049B6F0 + 0xD0): the 32-word list of func_002481B8
extern int D_01D93480;                       // [C] 0x01D93480 (.bss): semaphore id 0 (deleted by _sceMcCoreEnd)
extern int D_01D93484;                       // [C] 0x01D93484 (.bss): semaphore id 1 (SyncDma / SifCallback)
extern UnkStruct_McCoreSock D_01D93488[];    // [C] 0x01D93488 (.bss, 2 sockets of 0x1C bytes)
extern UnkStruct_McCoreReq D_01D934C0;       // [C] 0x01D934C0 (.bss, 0x2080): the request block
extern int D_01D95540[];                     // [C] 0x01D95540 (.bss): the reply header (0x80 bytes, memset 0x100 by the requests)
extern char D_01D955C0[];                    // [C] 0x01D955C0 (.bss): the reply data (read pages)
extern char D_01D98740[];                    // [C] 0x01D98740 (.bss, 0x1000): the netcnfif callback thread's buffer
extern UnkStruct_McNetSock D_01D99740;       // [C] 0x01D99740 (.bss)
extern int D_01D99760;                       // [C] 0x01D99760 (.bss) = D_01D99740.field_20
extern char D_004BB528[];                    // [C] 0x004BB528 (rodata) "** PANIC!\n"
extern char D_004BB538[];                    // [C] 0x004BB538 (rodata) "SendDataHoge\n"
extern char D_004BB548[];                    // [C] 0x004BB548 (rodata) "libmc2: Invalid data length (over 8192)\n"
extern char D_004BB578[];                    // [C] 0x004BB578 (rodata) "mc1"

extern "C" void SyncDCache(void *start, void *end);
extern "C" int iSignalSema(int id);
extern "C" void *memcpy(void *dst, const void *src, unsigned int n);
extern "C" int sceDbcGetConnection(int socket);
extern "C" int sceDbcCreateSocket(UnkStruct_McSocket *param, void *buf0, void *buf1);
extern "C" int sceDbcDeleteSocket(int socket);
extern "C" int sceDbcSendData3(int socket, unsigned int a1, int *size, unsigned char *src, unsigned int endfunc);
extern "C" int sceNetcnfifCreateCallbackThread(UnkStruct_McNcbParam *param, void *buf, int size, int arg);
extern "C" int sceNetcnfifDeleteCallbackThread(void);
extern "C" int sceMc2GetInfo(int socket, unsigned int a1);
extern "C" int sceMc2ReadFile(int socket, const char *name, unsigned int a2, unsigned int a3, unsigned int a4);
extern "C" int sceMc2CreateSocket(unsigned int *param);
extern "C" int sceMc2DeleteSocket(int socket);
extern "C" int sceMc2SearchSocket(unsigned int a0, unsigned int a1);
extern "C" int func_002430D0(int socket, const char *name, unsigned int a2, unsigned int a3, unsigned int a4,
                             unsigned int a5);
extern "C" int func_002496C0(void);          // [C] WaitSema(D_01D93484) (tail call, returns its result)
extern "C" int *SyncDma(int socket);
extern "C" int *SyncDmaForRead(int socket);
extern "C" int func_00248040(int socket, void *buf, unsigned int clust);
extern "C" int _sceMcpWriteUserClustOnCache(int socket, void *buf, unsigned int clust);
extern "C" void sceMc2SifCallback(void);
extern "C" int func_00248640(void);
extern "C" void initialize_mc2_socket(void);
extern "C" int create_mc2_socket(unsigned int a0);
extern "C" int delete_mc2_socket(void);
extern "C" int my_open(const char *a0, const char *path, int a2, int a3, int *out);
extern "C" int func_002498F8(int a0, int a1, const char *name, unsigned int a3, unsigned int a4, unsigned int a5);
extern "C" int func_00249928(void);

// [C] the 0x400-byte cluster copy loops (ldl / ldr / sdl / sdr or ld / sd, 32 bytes per round, the same bytes either way)
static inline void mc2_copy1k(char *dst, const char *src)
{
    int i;

    for (i = 0; i < 0x400; i++) {
        dst[i] = src[i];
    }
}

// [C] adds a cluster (clust + the param block's +0x34, then x pages-per-cluster / pages-per-block = block id s3) to the
// socket's user cluster cache (record D_01D8EC40 + socket * 0x2010: +0 first, +4 last, +8 block id, +0xC count, +0x10
// 8 slots of 0x400). Empty cache (+0 == -1): the record is set to {s1, s1, s3, 0} and the data copied to slot 0. The next
// cluster of the same block with room (count < 8): +4 = s1, copied to slot count. Otherwise func_00247C00(socket) (write the
// cache back), the record set as for an empty cache, slot 0 filled; its result is returned. Count is incremented;
// returns 1 unless the write-back ran (then its result). A zero pages-per-block (+0x2C) is not trapped (mc2_div).
// 0x00247C78
extern "C" int _sceMcpWriteUserClustOnCache(int socket, void *buf, unsigned int clust)
{
    char *tbl;
    char *rec;
    unsigned int s1;
    unsigned int ppc;
    int s3;
    int r;

    tbl = D_0049B6F0 + socket * 0x184;
    s1 = clust + *(unsigned int *)(tbl + 0x34);
    ppc = *(unsigned short *)(tbl + 0x2A);
    s3 = mc2_div((int)(s1 * ppc), *(unsigned short *)(tbl + 0x2C));
    rec = D_01D8EC40 + socket * 0x2010;
    if (*(int *)rec == -1) {
        *(unsigned int *)rec = s1;
        *(unsigned int *)(rec + 4) = s1;
        *(int *)(rec + 8) = s3;
        *(int *)(rec + 0xC) = 0;
        mc2_copy1k(rec + 0x10, (const char *)buf);
        *(int *)(rec + 0xC) += 1;
        return 1;
    }
    if (s1 == *(unsigned int *)(rec + 4) + 1 && *(int *)(rec + 0xC) < 8 && *(int *)(rec + 8) == s3) {
        *(unsigned int *)(rec + 4) = s1;
        mc2_copy1k(rec + 0x10 + (*(int *)(rec + 0xC) << 10), (const char *)buf);
        *(int *)(rec + 0xC) += 1;
        return 1;
    }
    r = func_00247C00(socket);
    *(unsigned int *)rec = s1;
    *(unsigned int *)(rec + 4) = s1;
    *(int *)(rec + 8) = s3;
    *(int *)(rec + 0xC) = 0;
    mc2_copy1k(rec + 0x10, (const char *)buf);
    *(int *)(rec + 0xC) += 1;
    return r;
}

// [C] reads a cluster through the cache: cluster = clust + param +0x34; when it lies in [rec+0, rec+0 + rec+0xC) the slot's
// the slot's 0x400 bytes are copied to buf and 1 returned; else tail call func_00247B70(socket, buf, cluster, 1).
// 0x00248040
extern "C" int func_00248040(int socket, void *buf, unsigned int clust)
{
    char *rec;
    int c;
    int first;

    rec = D_01D8EC40 + socket * 0x2010;
    first = *(int *)rec;
    c = (int)clust + *(int *)(D_0049B6F0 + socket * 0x184 + 0x34);
    if (c < first || !(c < first + *(int *)(rec + 0xC))) {
        return func_00247B70(socket, buf, (unsigned int)c, 1);
    }
    mc2_copy1k((char *)buf, rec + 0x10 + ((c - first) << 10));
    return 1;
}

// [C] tail call func_00248040(a0, a1, a2) (a3 ignored)
// 0x00248188
extern "C" int func_00248188(int socket, void *buf, unsigned int clust, unsigned int a3)
{
    return func_00248040(socket, buf, clust);
}

// [C] tail call _sceMcpWriteUserClustOnCache(a0, a1, a2) (a3 ignored)
// 0x002481A0
extern "C" int func_002481A0(int socket, void *buf, int clust, int count)
{
    return _sceMcpWriteUserClustOnCache(socket, buf, (unsigned int)clust);
}

// [C] 1 when the value a1 is one of the 32 words at the param block + 0xD0 (D_0049B7C0 + socket * 0x184), else 0.
// 0x002481B8
extern "C" int func_002481B8(int socket, int clust)
{
    int *p;
    int i;

    p = (int *)(D_0049B7C0 + socket * 0x184);
    for (i = 0; i < 0x20; i++) {
        if (p[i] == clust) {
            return 1;
        }
    }
    return 0;
}

// [C] a1 = a1 * (halfword +0x2A) / (halfword +0x2C) (div by zero: the asm traps, mc2_div here), tail call func_002481B8.
// 0x00248210
extern "C" int func_00248210(int socket, int clust)
{
    char *tbl;
    unsigned int ppc;

    tbl = D_0049B6F0 + socket * 0x184;
    ppc = *(unsigned short *)(tbl + 0x2A);
    return func_002481B8(socket, mc2_div((int)((unsigned int)clust * ppc), *(unsigned short *)(tbl + 0x2C)));
}

// [C] scans the card's blocks 1 .. count-1 (count = geom +8 / geom +4, at most 0x20 reported), reading 2 pages of each
// block (_sceMcCoreReadPage2(socket, D_01D92C80, geom+4 * block + k, 2, 0), k = 0, 2 ..): when any word of the first half
// of a page is not -1 the block is listed (its number stored through badlist, advancing by 4). Returns 1 when done (or
// the list holds 0x20), 0 when a read fails.
// 0x00248268
extern "C" int func_00248268(int socket, UnkStruct_McGeom *geom, int *badlist)
{
    int cnt;
    int blk;
    int n;
    int k;
    int used;
    int r;
    int a1;
    unsigned int half;

    cnt = 0;
    blk = 1;
    n = mc2_div(geom->field_8, geom->field_4);
    while (blk < n) {
        if (!(cnt < 0x20)) {
            return 1;
        }
        used = 0;
        for (k = 0; k < geom->field_4; k += 2) {
            r = _sceMcCoreReadPage2(socket, (unsigned int)D_01D92C80, (unsigned int)(blk * geom->field_4 + k), 2, 0);
            if (r == 0) {
                return 0;
            }
            half = (unsigned int)((geom->field_0 + ((unsigned int)geom->field_0 >> 31)) >> 1);
            for (a1 = 0; (unsigned int)a1 < half; a1++) {
                if (((int *)D_01D92C80)[a1] != -1) {
                    used++;
                    break;
                }
            }
        }
        if (used > 0) {
            *badlist = blk;
            badlist++;
            cnt++;
        }
        blk++;
    }
    return 1;
}

// [C] D_0049B9F8 = 0; WaitSema(D_01D93484); SyncDCache(D_01D95540, +0xFF); then the reply header word 0 (a0) and the word at
// D_01D955C0 (v1) decide: if D_0049B9F8 < a0 and v1 < a0: D_0049B9F8 = a0, returns D_01D95540; else if D_0049B9F8 < v1 and
// a0 < v1: D_0049B9F8 = v1, returns D_01D955C0; else sceDbcGetConnection(socket), word +8 of the header = 0, returns
// D_01D95540.
// 0x002483D8
extern "C" int *SyncDma(int socket)
{
    int *p;
    int a0;
    int v1;

    p = D_01D95540;
    D_0049B9F8 = 0;
    func_002496C0();
    SyncDCache(p, (char *)p + 0xFF);
    a0 = p[0];
    if (D_0049B9F8 < a0) {
        v1 = *(int *)D_01D955C0;
        if (v1 < a0) {
            D_0049B9F8 = a0;
            return p;
        }
    } else {
        v1 = *(int *)D_01D955C0;
    }
    if (D_0049B9F8 < v1 && a0 < v1) {
        D_0049B9F8 = v1;
        return (int *)D_01D955C0;
    }
    sceDbcGetConnection(socket);
    p[2] = 0;
    return p;
}

// [C] WaitSema(D_01D93484); SyncDCache(D_01D95540, +0x7F); header word 0 > 0: return D_01D95540; else
// sceDbcGetConnection(socket), word +8 = 0, return D_01D95540.
// 0x002484A8
extern "C" int *SyncDmaForRead(int socket)
{
    int *p;

    p = D_01D95540;
    func_002496C0();
    SyncDCache(p, (char *)p + 0x7F);
    if (p[0] > 0) {
        return p;
    }
    sceDbcGetConnection(socket);
    p[2] = 0;
    return p;
}

// [C] the libdbc end function of every core request: iSignalSema(D_01D93484), then `ei` (hand written in the asm).
// 0x00248518
extern "C" void sceMc2SifCallback(void)
{
    iSignalSema(D_01D93484);
    b3hw_ei();
}

// [C] D_0049B9F8 = 0; two CreateSema (max 0x7F, init 0 - the asm sets +4, +8, +0x14 only; the other words are the stack's:
// [G] 0) into D_01D93480 / D_01D93484; the 2 socket records: +0 = 0, +8 = 0x200, +0xC = 0x10, +0x10 = 0x4000. Returns 1.
// 0x00248548
extern "C" int _sceMcCoreInit(void)
{
    // boot2 E1: locals passed down to calls live on the guest stack at retail's offsets
    // [C 0x248548 addiu sp, -0x40; daddu a0, sp (CreateSema a0)]
    B3_RETAIL_FRAME(0x40);
    UnkStruct_SemaParam &p = *B3_FRAME_LOCAL(UnkStruct_SemaParam, 0);   // sp+0
    int i;

    p.field_0 = 0; // [G] uninitialised stack in the asm
    p.field_C = 0; // [G]
    p.field_10 = 0; // [G]
    p.field_8 = 0;
    p.field_4 = 0x7F;
    p.field_14 = 0;
    D_0049B9F8 = 0;
    D_01D93480 = CreateSema(&p);
    p.field_4 = 0x7F;
    p.field_8 = 0;
    p.field_14 = 0;
    D_01D93484 = CreateSema(&p);
    for (i = 1; i >= 0; i--) {
        UnkStruct_McCoreSock *rec = &D_01D93488[1 - i];
        rec->field_0 = 0;
        rec->field_8 = 0x200;
        rec->field_C = 0x10;
        rec->field_10 = 0x4000;
    }
    return 1;
}

// [C] DeleteSema(D_01D93480) when >= 0, then DeleteSema(D_01D93484) when >= 0. Returns 1.
// 0x002485F0
extern "C" int _sceMcCoreEnd(void)
{
    int a0;

    a0 = D_01D93480;
    if (a0 >= 0) {
        DeleteSema(a0);
    }
    a0 = D_01D93484;
    if (a0 >= 0) {
        DeleteSema(a0);
    }
    return 1;
}

// [C] index of the first socket record whose +0 is 0 (of 2), else -1.
// 0x00248640
extern "C" int func_00248640(void)
{
    int i;

    for (i = 0; i < 2; i++) {
        if (D_01D93488[i].field_0 == 0) {
            return i;
        }
    }
    return -1;
}

// [C] no free record: -1. Else the request {+0 = param[0] | 1, +4 = 2, +8 = param[2], +0xC = param[3], +0x10 = param[4],
// byte +0x14 = 0; the rest of the 0x24-byte parameter is stack garbage in the asm: [G] 0}, memset(D_01D95540, 0, 0x100),
// r = sceDbcCreateSocket(&req, D_01D95540, D_01D95540 + 0x80 bytes); rec.field_4 = r (stored before the test); r < 0: -1;
// else rec.field_0 = 1, returns the record index.
// 0x00248680
extern "C" int _sceMcCoreCreateSocket(UnkStruct_McSocket *s)
{
    // boot2 E1: locals passed down to calls live on the guest stack at retail's offsets
    // [C 0x248680 addiu sp, -0x50; daddu a0, sp (sceDbcCreateSocket a0)]
    B3_RETAIL_FRAME(0x50);
    UnkStruct_McSocket &req = *B3_FRAME_LOCAL(UnkStruct_McSocket, 0);   // sp+0
    int idx;
    int r;

    idx = func_00248640();
    if (idx < 0) {
        return -1;
    }
    req.field_C = s->field_C;
    req.field_0 = s->field_0 | 1;
    req.field_8 = s->field_8;
    req.field_10 = s->field_10;
    req.field_4 = 2;
    req.field_14 = 0;
    req.pad_15[0] = 0; // [G] the rest of the stack block (the asm leaves it): 0
    req.pad_15[1] = 0;
    req.pad_15[2] = 0;
    memset(D_01D95540, 0, 0x100);
    r = sceDbcCreateSocket(&req, D_01D95540, (char *)D_01D95540 + 0x80);
    D_01D93488[idx].field_4 = r;
    if (r < 0) {
        return -1;
    }
    D_01D93488[idx].field_0 = 1;
    return idx;
}

// [C] socket < 2 (unsigned) and its record +0 nonzero: 1, else 0.
// 0x00248750
extern "C" int func_00248750(int socket)
{
    if ((unsigned int)socket < 2) {
        return D_01D93488[socket].field_0 != 0;
    }
    return 0;
}

// [C] func_00248750(socket) true: sceDbcDeleteSocket(rec.field_4), rec.field_0 = 0, returns 1; else 0.
// 0x00248780
extern "C" int _sceMcCoreDeleteSocket(int socket)
{
    if (func_00248750(socket) != 0) {
        sceDbcDeleteSocket(D_01D93488[socket].field_4);
        D_01D93488[socket].field_0 = 0;
        return 1;
    }
    return 0;
}

// [C] index of the first created record (of 2) whose libdbc socket id (+4) is a0, else -1.
// 0x002487E0
extern "C" int func_002487E0(int a0)
{
    int i;

    for (i = 0; i < 2; i++) {
        if (D_01D93488[i].field_0 != 0 && D_01D93488[i].field_4 == a0) {
            return i;
        }
    }
    return -1;
}

// [C] copies the 16 bytes at params to the record's +8 .. +0x17 (read first, then written). Returns 1.
// 0x00248828
extern "C" int func_00248828(int socket, const void *params)
{
    const int *src;
    int w0, w1, w2, w3;
    UnkStruct_McCoreSock *rec;

    src = (const int *)params;
    w0 = src[0];
    w1 = src[1];
    w2 = src[2];
    w3 = src[3];
    rec = &D_01D93488[socket];
    rec->field_8 = (unsigned int)w0;
    rec->field_C = (unsigned int)w1;
    rec->field_10 = (unsigned int)w2;
    rec->field_14 = (unsigned int)w3;
    return 1;
}

// [C] not connected (sceDbcGetConnection(rec.field_4) == 3): 0. Else request op 3: header +0x24 .. +0x08 .. +0x20 = 0,
// memset(D_01D95540, 0, 0x100), SyncDCache(D_01D95540, +0xFF), cmdno = 3, send until accepted, SyncDma. Reply word +8
// zero: 0. Else rec +8 = the u16 at reply byte 0xC, +0xC = the u16 at 0xE, +0x10 = the u32 at 0x10 (little endian bytes),
// +0x14 = 1, and the 16 bytes rec+8 .. rec+0x17 are copied to out. Returns 1.
// 0x00248880
extern "C" int _sceMcCoreGetCardSpec(int socket, UnkStruct_McGeom *out)
{
    // boot2 E1: locals passed down to calls live on the guest stack at retail's offsets
    // [C 0x248880 addiu sp, -0x60; sw v1, 0x0(sp); daddu a2, sp (sceDbcSendData3 a2)]
    B3_RETAIL_FRAME(0x60);
    UnkStruct_McCoreSock *rec;
    int *hdr = B3_FRAME_LOCAL(int, 0);   // sp+0 (only hdr[0] is used)
    int *rep;
    unsigned char *rb;
    int w0, w1, w2, w3;
    int *o;

    rec = &D_01D93488[socket];
    if (sceDbcGetConnection(rec->field_4) == 3) {
        return 0;
    }
    hdr[0] = 0x2080;
    D_01D934C0.field_24 = 0;
    D_01D934C0.field_8 = 0;
    D_01D934C0.field_C = 0;
    D_01D934C0.field_10 = 0;
    D_01D934C0.field_14 = 0;
    D_01D934C0.field_18 = 0;
    D_01D934C0.field_1C = 0;
    D_01D934C0.field_20 = 0;
    memset(D_01D95540, 0, 0x100);
    SyncDCache(D_01D95540, (char *)D_01D95540 + 0xFF);
    cmdno = 3;
    while (sceDbcSendData3(rec->field_4, cmdno, hdr, (unsigned char *)&D_01D934C0, (unsigned int)sceMc2SifCallback) != 1) {
    }
    rep = SyncDma(D_01D93488[socket].field_4);
    if (rep[2] == 0) {
        return 0;
    }
    rb = (unsigned char *)rep;
    rec = &D_01D93488[socket];
    rec->field_8 = rb[0xC] + (rb[0xD] << 8);
    rec->field_C = rb[0xE] + (rb[0xF] << 8);
    rec->field_10 = rb[0x10] + ((unsigned int)rb[0x13] << 24) + (rb[0x11] << 8) + (rb[0x12] << 16);
    rec->field_14 = 1;
    w0 = (int)rec->field_8;
    w1 = (int)rec->field_C;
    w2 = (int)rec->field_10;
    w3 = (int)rec->field_14;
    o = (int *)out;
    o[0] = w0;
    o[1] = w1;
    o[2] = w2;
    o[3] = w3;
    return 1;
}

// [C] op 4 (no payload): not connected: 0. Header +0x10 / +8 / +0xC = rec +0x10 / +8 / +0xC, q = (+0x10) / (+0xC) (zero
// divisor traps in the asm, mc2_div here), +0x14 = q - 1, +0x18 = q - 2, memset(D_01D95540, 0, 0x100), cmdno = 4, send,
// SyncDma. Returns the reply word +8.
// 0x00248A68
extern "C" int func_00248A68(int socket)
{
    // boot2 E1: locals passed down to calls live on the guest stack at retail's offsets
    // [C 0x248A68 addiu sp, -0x60; sw t2, 0x0(sp); daddu a2, sp (sceDbcSendData3 a2)]
    B3_RETAIL_FRAME(0x60);
    UnkStruct_McCoreSock *rec;
    int *hdr = B3_FRAME_LOCAL(int, 0);   // sp+0 (only hdr[0] is used)
    int *rep;
    int q;

    rec = &D_01D93488[socket];
    if (sceDbcGetConnection(rec->field_4) == 3) {
        return 0;
    }
    D_01D934C0.field_10 = rec->field_10;
    D_01D934C0.field_8 = rec->field_8;
    D_01D934C0.field_C = rec->field_C;
    q = mc2_div((int)rec->field_10, (int)rec->field_C);
    D_01D934C0.field_14 = (unsigned int)(q - 1);
    D_01D934C0.field_18 = (unsigned int)(q - 2);
    hdr[0] = 0x2080;
    memset(D_01D95540, 0, 0x100);
    cmdno = 4;
    while (sceDbcSendData3(rec->field_4, cmdno, hdr, (unsigned char *)&D_01D934C0, (unsigned int)sceMc2SifCallback) != 1) {
    }
    rep = SyncDma(D_01D93488[socket].field_4);
    return rep[2];
}

// [C] op 0xC (no request fields): not connected: 0. memset(D_01D95540, 0, 0x100), SyncDCache(D_01D95540, +0xFF), cmdno =
// op 0xC, send, SyncDma. Returns (reply word +8 != 0).
// 0x00248BB8
extern "C" int _sceMcCoreCheckNewCard(int socket)
{
    // boot2 E1: locals passed down to calls live on the guest stack at retail's offsets
    // [C 0x248BB8 addiu sp, -0x60; sw v0, 0x0(sp); daddu a2, sp (sceDbcSendData3 a2)]
    B3_RETAIL_FRAME(0x60);
    UnkStruct_McCoreSock *rec;
    int *hdr = B3_FRAME_LOCAL(int, 0);   // sp+0 (only hdr[0] is used)
    int *rep;

    rec = &D_01D93488[socket];
    if (sceDbcGetConnection(rec->field_4) == 3) {
        return 0;
    }
    hdr[0] = 0x2080;
    memset(D_01D95540, 0, 0x100);
    SyncDCache(D_01D95540, (char *)D_01D95540 + 0xFF);
    cmdno = 0xC;
    while (sceDbcSendData3(rec->field_4, cmdno, hdr, (unsigned char *)&D_01D934C0, (unsigned int)sceMc2SifCallback) != 1) {
    }
    rep = SyncDma(D_01D93488[socket].field_4);
    return rep[2] != 0;
}

// [C] op 6: not connected: 0. rec +8 or +0xC zero: scePrintf("** PANIC!\n"), 0. Else header +8 / +0x10 / +0xC = rec
// +8 / +0x10 / +0xC, q = (+0x10) / (+0xC), +0x14 = q - 1, +0x18 = q - 2, +0x24 = rec +0x14, memset(D_01D95540, 0, 0x100),
// cmdno = 6, send, SyncDma. Returns the reply word +8.
// 0x00248CD0
extern "C" int _sceMcCoreWriteBack(int socket)
{
    // boot2 E1: locals passed down to calls live on the guest stack at retail's offsets
    // [C 0x248CD0 addiu sp, -0x60; sw v1, 0x0(sp); daddu a2, sp (sceDbcSendData3 a2)]
    B3_RETAIL_FRAME(0x60);
    UnkStruct_McCoreSock *rec;
    int *hdr = B3_FRAME_LOCAL(int, 0);   // sp+0 (only hdr[0] is used)
    int *rep;
    int q;

    rec = &D_01D93488[socket];
    if (sceDbcGetConnection(rec->field_4) == 3) {
        return 0;
    }
    if (rec->field_8 == 0 || rec->field_C == 0) {
        scePrintf(D_004BB528);
        return 0;
    }
    D_01D934C0.field_8 = rec->field_8;
    D_01D934C0.field_10 = rec->field_10;
    D_01D934C0.field_C = rec->field_C;
    q = mc2_div((int)rec->field_10, (int)rec->field_C);
    D_01D934C0.field_14 = (unsigned int)(q - 1);
    D_01D934C0.field_18 = (unsigned int)(q - 2);
    D_01D934C0.field_24 = rec->field_14;
    hdr[0] = 0x2080;
    memset(D_01D95540, 0, 0x100);
    cmdno = 6;
    while (sceDbcSendData3(rec->field_4, cmdno, hdr, (unsigned char *)&D_01D934C0, (unsigned int)sceMc2SifCallback) != 1) {
    }
    rep = SyncDma(D_01D93488[socket].field_4);
    return rep[2];
}

// [C] op 0xA (no request fields): not connected: 0. memset(D_01D95540, 0, 0x100), cmdno = 0xA, send, SyncDma. Reply word
// +8 zero: 0; else the 8 bytes at reply +0xC are copied to out and the reply word +8 returned.
// 0x00248E50
extern "C" int _sceMcCoreReadClock(int socket, unsigned char *out)
{
    // boot2 E1: locals passed down to calls live on the guest stack at retail's offsets
    // [C 0x248E50 addiu sp, -0x60; sw v0, 0x0(sp); daddu a2, sp (sceDbcSendData3 a2)]
    B3_RETAIL_FRAME(0x60);
    UnkStruct_McCoreSock *rec;
    int *hdr = B3_FRAME_LOCAL(int, 0);   // sp+0 (only hdr[0] is used)
    int *rep;
    unsigned char t[8];
    int i;

    rec = &D_01D93488[socket];
    if (sceDbcGetConnection(rec->field_4) == 3) {
        return 0;
    }
    hdr[0] = 0x2080;
    memset(D_01D95540, 0, 0x100);
    cmdno = 0xA;
    while (sceDbcSendData3(rec->field_4, cmdno, hdr, (unsigned char *)&D_01D934C0, (unsigned int)sceMc2SifCallback) != 1) {
    }
    rep = SyncDma(D_01D93488[socket].field_4);
    if (rep[2] == 0) {
        return 0;
    }
    for (i = 0; i < 8; i++) {
        t[i] = ((unsigned char *)rep)[0xC + i];
    }
    for (i = 0; i < 8; i++) {
        out[i] = t[i];
    }
    return rep[2];
}

// [C] op 1, reads `count` pages starting at page a2 into buf (a1): not connected: 0. Header +0 = a2, +4 = count, +8 / +0xC
// / +0x10 = rec +8 / +0xC / +0x10, q = (+0x10) / (+0xC), +0x14 = q - 1, +0x18 = q - 2, +0x1C = +0x20 = 0, +0x24 = a4,
// memset(D_01D95540, 0, 0x100), cmdno = 1; sceDbcSendData3 until accepted (scePrintf("SendDataHoge\n") after each
// refusal), SyncDmaForRead. Reply word +8 zero: 0. Else for each page: SyncDCache(D_01D955C0 + n * pagesize, + pagesize),
// memcpy(buf + n * pagesize, D_01D955C0 + n * pagesize, header +8). Returns count.
// 0x00248F80
extern "C" int _sceMcCoreReadPage2(int socket, unsigned int a1, unsigned int a2, int a3, unsigned int a4)
{
    // boot2 E1: locals passed down to calls live on the guest stack at retail's offsets
    // [C 0x248F80 addiu sp, -0x60; sw t2, 0x0(sp); daddu a2, sp (sceDbcSendData3 a2)]
    B3_RETAIL_FRAME(0x60);
    UnkStruct_McCoreSock *rec;
    int *hdr = B3_FRAME_LOCAL(int, 0);   // sp+0 (only hdr[0] is used)
    int *rep;
    int q;
    int n;
    char *src;
    char *dst;

    rec = &D_01D93488[socket];
    if (sceDbcGetConnection(rec->field_4) == 3) {
        return 0;
    }
    D_01D934C0.field_10 = rec->field_10;
    D_01D934C0.field_0 = a2;
    D_01D934C0.field_8 = rec->field_8;
    D_01D934C0.field_4 = (unsigned int)a3;
    D_01D934C0.field_C = rec->field_C;
    q = mc2_div((int)rec->field_10, (int)rec->field_C);
    D_01D934C0.field_14 = (unsigned int)(q - 1);
    D_01D934C0.field_24 = a4;
    D_01D934C0.field_18 = (unsigned int)(q - 2);
    hdr[0] = 0x2080;
    D_01D934C0.field_1C = 0;
    D_01D934C0.field_20 = 0;
    memset(D_01D95540, 0, 0x100);
    cmdno = 1;
    while (sceDbcSendData3(D_01D93488[socket].field_4, cmdno, hdr, (unsigned char *)&D_01D934C0, (unsigned int)sceMc2SifCallback) != 1) {
        scePrintf(D_004BB538);
    }
    rep = SyncDmaForRead(D_01D93488[socket].field_4);
    if (rep[2] == 0) {
        return 0;
    }
    dst = (char *)a1;
    src = D_01D955C0;
    if (a3 > 0) {
        n = a3;
        do {
            SyncDCache(src, src + D_01D934C0.field_8);
            n--;
            memcpy(dst, src, D_01D934C0.field_8);
            dst += D_01D934C0.field_8;
            src += D_01D934C0.field_8;
        } while (n != 0);
    }
    return a3;
}

// [C] tail call _sceMcCoreReadPage2(socket, buf, page, count, a4 = rec +0x14).
// 0x00249160
extern "C" int func_00249160(int socket, void *buf, unsigned int page, unsigned int count)
{
    return _sceMcCoreReadPage2(socket, (unsigned int)buf, page, (int)count, D_01D93488[socket].field_14);
}

// [C] op 2: not connected: 0. page size (rec +8) * count (a3) > 0x2000: scePrintf("libmc2: Invalid data length (over
// 8192)\n"), 0. Header +0xC / +0x10 = rec +0xC / +0x10, +0 = a2 (first page), +8 = page size, +4 = count, +0x14 = q - 1,
// +0x18 = q - 2, +0x24 = rec +0x14, +0x1C = +0x20 = 0, q = (+0x10) / (+0xC); memcpy(D_01D934C0 + 0x28, buf, size * count);
// memset(D_01D95540, 0, 0x100), SyncDCache(D_01D95540, +0xFF), cmdno = 2, send, SyncDma. Returns count when the reply
// word +8 is nonzero, else 0.
// 0x00249190
extern "C" int _sceMcCoreWritePage(int socket, unsigned int a1, unsigned int a2, unsigned int a3)
{
    // boot2 E1: locals passed down to calls live on the guest stack at retail's offsets
    // [C 0x249190 addiu sp, -0x60; sw v0, 0x0(sp); daddu a2, sp (sceDbcSendData3 a2)]
    B3_RETAIL_FRAME(0x60);
    UnkStruct_McCoreSock *rec;
    int *hdr = B3_FRAME_LOCAL(int, 0);   // sp+0 (only hdr[0] is used)
    int *rep;
    int q;
    unsigned int bytes;
    unsigned int psize;

    rec = &D_01D93488[socket];
    if (sceDbcGetConnection(rec->field_4) == 3) {
        return 0;
    }
    psize = rec->field_8;
    bytes = psize * a3;
    if (bytes >= 0x2001) {
        scePrintf(D_004BB548);
        return 0;
    }
    D_01D934C0.field_C = rec->field_C;
    D_01D934C0.field_10 = rec->field_10;
    D_01D934C0.field_0 = a2;
    D_01D934C0.field_8 = psize;
    D_01D934C0.field_4 = a3;
    q = mc2_div((int)rec->field_10, (int)rec->field_C);
    D_01D934C0.field_14 = (unsigned int)(q - 1);
    D_01D934C0.field_18 = (unsigned int)(q - 2);
    D_01D934C0.field_24 = rec->field_14;
    D_01D934C0.field_1C = 0;
    D_01D934C0.field_20 = 0;
    memcpy(D_01D934C0.field_28, (void *)a1, bytes);
    hdr[0] = 0x2080;
    memset(D_01D95540, 0, 0x100);
    SyncDCache(D_01D95540, (char *)D_01D95540 + 0xFF);
    cmdno = 2;
    while (sceDbcSendData3(rec->field_4, cmdno, hdr, (unsigned char *)&D_01D934C0, (unsigned int)sceMc2SifCallback) != 1) {
    }
    rep = SyncDma(D_01D93488[socket].field_4);
    return rep[2] != 0 ? (int)a3 : 0;
}

// [C] op 0xB: not connected: 0. Header +0 = a1, +4 = a2, +8 / +0xC / +0x10 = rec +8 / +0xC / +0x10, q = (+0x10) / (+0xC),
// +0x14 = q - 1, +0x18 = q - 2, +0x24 = rec +0x14, +0x1C = +0x20 = 0, memset(D_01D95540, 0, 0x100), cmdno = 0xB, send,
// SyncDma. Returns a2 when the reply word +8 is nonzero, else 0.
// 0x00249358
extern "C" int _sceMcCoreWriteSameData(int socket, unsigned int a1, unsigned int a2)
{
    // boot2 E1: locals passed down to calls live on the guest stack at retail's offsets
    // [C 0x249358 addiu sp, -0x60; sw t1, 0x0(sp); daddu a2, sp (sceDbcSendData3 a2)]
    B3_RETAIL_FRAME(0x60);
    UnkStruct_McCoreSock *rec;
    int *hdr = B3_FRAME_LOCAL(int, 0);   // sp+0 (only hdr[0] is used)
    int *rep;
    int q;

    rec = &D_01D93488[socket];
    if (sceDbcGetConnection(rec->field_4) == 3) {
        return 0;
    }
    D_01D934C0.field_8 = rec->field_8;
    D_01D934C0.field_0 = a1;
    D_01D934C0.field_10 = rec->field_10;
    D_01D934C0.field_4 = a2;
    D_01D934C0.field_C = rec->field_C;
    q = mc2_div((int)rec->field_10, (int)rec->field_C);
    D_01D934C0.field_14 = (unsigned int)(q - 1);
    D_01D934C0.field_18 = (unsigned int)(q - 2);
    D_01D934C0.field_24 = rec->field_14;
    hdr[0] = 0x2080;
    D_01D934C0.field_1C = 0;
    D_01D934C0.field_20 = 0;
    memset(D_01D95540, 0, 0x100);
    cmdno = 0xB;
    while (sceDbcSendData3(rec->field_4, cmdno, hdr, (unsigned char *)&D_01D934C0, (unsigned int)sceMc2SifCallback) != 1) {
    }
    rep = SyncDma(D_01D93488[socket].field_4);
    return rep[2] != 0 ? (int)a2 : 0;
}

// [C] op 9: not connected: 0. Header +0x20 = block, +4 = 1, +8 / +0xC / +0x10 = rec +8 / +0xC / +0x10, +0 = +0x1C = 0,
// memset(D_01D95540, 0, 0x100), SyncDCache(D_01D95540, +0xFF), cmdno = 9, send, SyncDma. Returns (reply word +8 != 0).
// 0x002494D8
extern "C" int _sceMcCoreBlockErase(int socket, int block)
{
    // boot2 E1: locals passed down to calls live on the guest stack at retail's offsets
    // [C 0x2494D8 addiu sp, -0x60; sw v1, 0x0(sp); daddu a2, sp (sceDbcSendData3 a2)]
    B3_RETAIL_FRAME(0x60);
    UnkStruct_McCoreSock *rec;
    int *hdr = B3_FRAME_LOCAL(int, 0);   // sp+0 (only hdr[0] is used)
    int *rep;

    rec = &D_01D93488[socket];
    if (sceDbcGetConnection(rec->field_4) == 3) {
        return 0;
    }
    D_01D934C0.field_20 = (unsigned int)block;
    hdr[0] = 0x2080;
    D_01D934C0.field_4 = 1;
    D_01D934C0.field_8 = rec->field_8;
    D_01D934C0.field_C = rec->field_C;
    D_01D934C0.field_10 = rec->field_10;
    D_01D934C0.field_0 = 0;
    D_01D934C0.field_1C = 0;
    memset(D_01D95540, 0, 0x100);
    SyncDCache(D_01D95540, (char *)D_01D95540 + 0xFF);
    cmdno = 9;
    while (sceDbcSendData3(rec->field_4, cmdno, hdr, (unsigned char *)&D_01D934C0, (unsigned int)sceMc2SifCallback) != 1) {
    }
    rep = SyncDma(D_01D93488[socket].field_4);
    return rep[2] != 0;
}

// [C] returns 1
// 0x00249638
extern "C" int func_00249638(void)
{
    return 1;
}

// [C] sceDbcGetConnection(rec.field_4) != 3
// 0x00249640
extern "C" int func_00249640(int socket)
{
    return sceDbcGetConnection(D_01D93488[socket].field_4) != 3;
}

// [C] tail call WaitSema(D_01D93484)
// 0x00249680
extern "C" int func_00249680(void)
{
    return WaitSema(D_01D93484);
}

// [C] tail call SignalSema(D_01D93484)
// 0x002496A0
extern "C" int func_002496A0(void)
{
    return SignalSema(D_01D93484);
}

// [C] tail call WaitSema(D_01D93484)
// 0x002496C0
extern "C" int func_002496C0(void)
{
    return WaitSema(D_01D93484);
}

// [C] the netcnfif side: memset(D_01D99740, 0, 0x28), +4 = -1, +0x24 = 0 (v0 is memset's result; callers ignore it).
// 0x002496E0
extern "C" void initialize_mc2_socket(void)
{
    memset(&D_01D99740, 0, 0x28);
    D_01D99740.field_4 = -1;
    D_01D99740.field_24 = 0;
}

// [C] sceMc2SearchSocket(a0, 0) >= 0: +0x20 = it, +0x24 += 1, returns 0. Else if +4 != -1: +0x24 += 1, 0. Else +4 = a0,
// +8 = 0, +0 = 2, r = sceMc2CreateSocket(&D_01D99740) (the asm also passes D_01D99780 as a second argument, which
// the callee ignores); r < 0: -1; else +0x20 = r, +0x24 += 1, 0.
// 0x00249720
extern "C" int create_mc2_socket(unsigned int a0)
{
    int r;

    r = sceMc2SearchSocket(a0, 0);
    if (r >= 0) {
        D_01D99740.field_20 = r;
    } else {
        if (D_01D99740.field_4 == -1) {
            D_01D99740.field_4 = (int)a0;
            D_01D99740.field_8 = 0;
            D_01D99740.field_0 = 2;
            r = sceMc2CreateSocket((unsigned int *)&D_01D99740);
            if (r < 0) {
                return -1;
            }
            D_01D99740.field_20 = r;
        }
    }
    D_01D99740.field_24 += 1;
    return 0;
}

// [C] --D_01D99740.field_24; when it reaches 0 or below: if +4 != -1 sceMc2DeleteSocket(+0x20); initialize_mc2_socket().
// Returns 0.
// 0x002497D0
extern "C" int delete_mc2_socket(void)
{
    int v;

    v = D_01D99740.field_24 - 1;
    D_01D99740.field_24 = v;
    if (v > 0) {
        return 0;
    }
    if (D_01D99740.field_4 != -1) {
        sceMc2DeleteSocket(D_01D99740.field_20);
    }
    initialize_mc2_socket();
    return 0;
}

// [C] the open callback of sceNetcnfif: create_mc2_socket(strncmp(a0, "mc1", 3) != 0 ? 2 : 3) < 0: -1. r =
// sceMc2GetInfo(D_01D99740.field_20, 0); when r >= 0 or (r & 0xFFFF) == 0x9003: func_002430D0(socket, path, 0, 1, ent,
// &cnt) >= 0 and cnt == 1: *out = ent word +0x10, returns D_01D99740.field_24. Any other case: delete_mc2_socket(), -1.
// ent (0x40 bytes) and cnt are the stack's in the asm, written by the directory call; [G] zero-initialised here.
// 0x00249820
extern "C" int my_open(const char *a0, const char *path, int a2, int a3, int *out)
{
    // boot2 E1: `ent` and `cnt` go to func_002430D0 as integer words: guest-stack locals at retail's offsets [C 0x249820
    // addiu sp, -0x70; ent at sp+0 (lw v0, 0x10(sp) reads ent[4]), cnt at sp+0x40 (addiu t1, sp, 0x40)]
    B3_RETAIL_FRAME(0x70);
    int *ent = B3_FRAME_LOCAL(int, 0);
    int &cnt = *B3_FRAME_LOCAL(int, 0x40);
    int r;
    int i;

    r = strncmp(a0, D_004BB578, 3);
    if (create_mc2_socket(r != 0 ? 2 : 3) < 0) {
        return -1;
    }
    for (i = 0; i < 16; i++) {
        ent[i] = 0; // [G]
    }
    cnt = 0; // [G]
    r = sceMc2GetInfo(D_01D99740.field_20, 0);
    if (r >= 0 || (r & 0xFFFF) == 0x9003) {
        if (func_002430D0(D_01D99740.field_20, path, 0, 1, (unsigned int)ent, (unsigned int)&cnt) >= 0) {
            if (cnt == 1) {
                *out = ent[4];
                return D_01D99740.field_24;
            }
        }
    }
    delete_mc2_socket();
    return -1;
}

// [C] sceMc2ReadFile(D_01D99760, name, a3, a4, a5): the a0 / a1 arguments are dropped, the others shifted down.
// 0x002498F8
extern "C" int func_002498F8(int a0, int a1, const char *name, unsigned int a3, unsigned int a4, unsigned int a5)
{
    return sceMc2ReadFile(D_01D99760, name, a3, a4, a5);
}

// [C] delete_mc2_socket(); 0
// 0x00249928
extern "C" int func_00249928(void)
{
    delete_mc2_socket();
    return 0;
}

// [C] param {+0 = 1, +4 = my_open, +8 = func_002498F8, +0xC = func_00249928}; initialize_mc2_socket();
// sceNetcnfifCreateCallbackThread(&param, D_01D98740, 0x1000, a0); returns its result.
// 0x00249948
extern "C" int sceNetcnfifCreateMc2Thread(int a0)
{
    // boot2 E1: locals passed down to calls live on the guest stack at retail's offsets
    // [C 0x249954 addiu sp, -0x20; daddu a0, sp (sceNetcnfifCreateCallbackThread a0)]
    B3_RETAIL_FRAME(0x20);
    UnkStruct_McNcbParam &p = *B3_FRAME_LOCAL(UnkStruct_McNcbParam, 0);   // sp+0

    p.field_8 = func_002498F8;
    p.field_C = func_00249928;
    p.field_4 = my_open;
    p.field_0 = 1;
    initialize_mc2_socket();
    return sceNetcnfifCreateCallbackThread(&p, D_01D98740, 0x1000, a0);
}

// [C] sceNetcnfifDeleteCallbackThread(); 0
// 0x002499B0
extern "C" int sceNetcnfifDeleteMc2Thread(void)
{
    sceNetcnfifDeleteCallbackThread();
    return 0;
}
