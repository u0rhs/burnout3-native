// Sony EE library libdbc (SDK 2.8.0 as retail links it: the .data id string at 0x0049B0F0 reads "PsIIlibdbc  2800"):
// the EE side of the IOP's DBCMAN (device sockets for libpad2 pads and libmc2 memory cards). Written from the retail
// asm (decomp assembly/asm/sce/libdbc.s, data asm/data/sce/libdbc.{data,bss,rodata}.s). Tags: [C] confirmed from the
// asm, [L] likely, [G] guess.
// Three RPC clients: D_006797C8 bound to DBCMAN 0x80001300 (every request below with function number 0x800013xx,
// send = receive = D_00679840, 0x280 bytes, waiting), D_006797F0 to 0x8000131B (sceDbcSendData2: D_00679AC0, 0x1090,
// no wait) and D_00679818 to 0x8000131C (sceDbcSendData3: D_0067ABC0, 0x2090, no wait, the caller's end function).
// DBCMAN publishes one status word per socket in the work area registered with sceDbcSetWorkAddr (D_0067CCC0; 1 =
// connected), which sceDbcGetDepNumber / sceDbcGetConnection copy to D_0067CD40 with interrupts off. The IOP side is
// the native platform's (src/native/platform_sif.cpp, "dbc").
// boot2 (2026-11-06).

// [C] the libsif client (sceSifClientData, 0x28 bytes): the bind loops wait for +0x24 (the server) to become nonzero.
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
    unsigned int field_24;      // [C] the bound server (0 until the IOP answers the bind)
};

// [C] the semaphore parameter on sceDbcInit's stack ([L] SemaParam: current, max, init, ...): the asm sets max and
// init only.
struct UnkStruct_SemaParam {
    int field_0;
    int field_4;    // [C] 0x7F
    int field_8;    // [C] 1
    int field_C;
    unsigned int field_10;
    unsigned int field_14;
};

extern int isInit;                          // [C] 0x0049B100 (.data): 1 after sceDbcInit
extern UnkStruct_SifClient D_006797C8;      // [C] 0x006797C8: client of DBCMAN 0x80001300
extern UnkStruct_SifClient D_006797F0;      // [C] 0x006797F0: client of 0x8000131B
extern UnkStruct_SifClient D_00679818;      // [C] 0x00679818: client of 0x8000131C
extern unsigned int D_00679840[];           // [C] 0x00679840: the request / reply buffer (0x280 sent; sceDbcSRData reads to +0x410)
extern unsigned int D_00679AC0[];           // [C] 0x00679AC0: sceDbcSendData2's buffer (0x1090)
extern unsigned int D_0067ABC0[];           // [C] 0x0067ABC0: sceDbcSendData3's buffer (0x2090)
extern unsigned int D_0067CCC0[];           // [C] 0x0067CCC0: the work area DBCMAN writes (0x80; 16 status words used)
extern unsigned int D_0067CD40[];           // [C] 0x0067CD40: the status words' copy (16)
extern int D_0067CD80;                      // [C] 0x0067CD80: the request semaphore
extern char D_004B9920[];                   // [C] 0x004B9920 .. 0x004B9B88: messages (rodata)
extern char D_004B9938[];
extern char D_004B9960[];
extern char D_004B9988[];
extern char D_004B99A8[];
extern char D_004B99C8[];
extern char D_004B99E8[];
extern char D_004B9A08[];
extern char D_004B9A28[];
extern char D_004B9A48[];
extern char D_004B9A68[];
extern char D_004B9A90[];
extern char D_004B9AB0[];
extern char D_004B9AD0[];
extern char D_004B9B00[];
extern char D_004B9B20[];
extern char D_004B9B40[];
extern char D_004B9B68[];
extern char D_004B9B88[];

extern "C" int scePrintf(const char *fmt, ...);
extern "C" void sceSifInitRpc(unsigned int mode);
extern "C" int sceSifBindRpc(UnkStruct_SifClient *cd, unsigned int id, unsigned int mode);
extern "C" int sceSifCallRpc(UnkStruct_SifClient *cd, unsigned int fno, unsigned int mode, void *send, int ssize,
                             void *recv, int rsize, unsigned int endfunc, unsigned int endpara);
extern "C" int sceSifCheckStatRpc(UnkStruct_SifClient *cd);
extern "C" int CreateSema(UnkStruct_SemaParam *param);
extern "C" int DeleteSema(int id);
extern "C" int WaitSema(int id);
extern "C" int SignalSema(int id);
extern "C" void SyncDCache(void *start, void *end);
extern "C" int func_00114E60(void);         // [C] DIntr
extern "C" int EIntr(void);

extern "C" int sceDbcGetModVersion(void);
extern "C" int sceDbcSetWorkAddr(void *addr);
extern "C" void DPRINT(const char *fmt, unsigned int a1);

// the request every 0x800013xx function sends: fno on client D_006797C8, D_00679840 both ways, 0x280 bytes, waiting
static inline int dbc_call(unsigned int fno)
{
    return sceSifCallRpc(&D_006797C8, fno, 0, D_00679840, 0x280, D_00679840, 0x280, 0, 0);
}

// [C] the copy of the work area's first 64 bytes into D_0067CD40 with interrupts off (sceDbcGetDepNumber,
// sceDbcGetConnection): SyncDCache(work, work + 0x80), DIntr, 8 doublewords, EIntr.
static inline void dbc_copy_status(void)
{
    int i;

    SyncDCache(D_0067CCC0, (unsigned char *)D_0067CCC0 + 0x80);
    func_00114E60();
    for (i = 0; i < 16; i++) {
        D_0067CD40[i] = D_0067CCC0[i];
    }
    EIntr();
}

// [C] request 0x80001363 (no error check); returns the reply's +0 (DBCMAN's version: sceDbcInit wants 3 in bits 8+).
// 0x00215F18
extern "C" int sceDbcGetModVersion(void)
{
    dbc_call(0x80001363);
    return (int)D_00679840[0];
}

// [C] already initialised: 1. Else D_0067CD80 = 0, sceSifInitRpc(0), the three binds (each retried after a delay loop
// until the client's +0x24 is set; a negative bind: the message, 0), the version check (bits 8+ != 3: two messages,
// 0), CreateSema {max 0x7F, init 1} into D_0067CD80 (< 0: 0), D_0067CD40 cleared (index 15 down to 0), DPRINT, the
// work area registered, isInit = 1. Returns 1.
// 0x00215F70
extern "C" int sceDbcInit(void)
{
    UnkStruct_SemaParam p;
    int ver;
    int hi;
    int id;
    int i;
    int d;

    if (isInit == 1) {
        return 1;
    }
    D_0067CD80 = 0;
    sceSifInitRpc(0);
    for (;;) {
        if (sceSifBindRpc(&D_006797C8, 0x80001300, 0) < 0) {
            goto fail;
        }
        if (D_006797C8.field_24 != 0) {
            break;
        }
        for (d = 0x10000; d != -1; d--) { // [C] 0x215FB8: a busy delay (0x10001 iterations of nops)
        }
    }
    for (;;) {
        if (sceSifBindRpc(&D_006797F0, 0x8000131B, 0) < 0) {
            goto fail;
        }
        if (D_006797F0.field_24 != 0) {
            break;
        }
        for (d = 0x10000; d != -1; d--) {
        }
    }
    for (;;) {
        if (sceSifBindRpc(&D_00679818, 0x8000131C, 0) < 0) {
            goto fail;
        }
        if (D_00679818.field_24 != 0) {
            break;
        }
        for (d = 0x10000; d != -1; d--) {
        }
    }
    ver = sceDbcGetModVersion();
    hi = ver >> 8;
    if (hi != 3) {
        scePrintf(D_004B9938);
        scePrintf(D_004B9960, 3, 0, hi, ver & 0xFF);
        return 0;
    }
    p.field_0 = 0; // [G] not written by the asm (the stack's value; host: 0)
    p.field_C = 0;
    p.field_10 = 0;
    p.field_14 = 0;
    p.field_8 = 1;
    p.field_4 = 0x7F;
    id = CreateSema(&p);
    D_0067CD80 = id;
    if (id < 0) {
        return 0;
    }
    for (i = 15; i >= 0; i--) {
        D_0067CD40[i] = 0;
    }
    DPRINT(D_004B9988, (unsigned int)D_0067CCC0);
    sceDbcSetWorkAddr(D_0067CCC0);
    isInit = 1;
    return 1;
fail:
    DPRINT(D_004B9920, 0);
    return 0;
}

// [C] not initialised: 0. Else isInit = 0 and the semaphore deleted when >= 0. Returns 1.
// 0x002161B0
extern "C" int sceDbcEnd(void)
{
    if (isInit == 0) {
        return 0;
    }
    isInit = 0;
    if (D_0067CD80 < 0) {
        return 1;
    }
    DeleteSema(D_0067CD80);
    return 1;
}

// [C] request 0x80001304 with +4 = the work area; returns the reply's +0, or (a negative call) the message and 0.
// 0x002161F8
extern "C" int sceDbcSetWorkAddr(void *addr)
{
    D_00679840[1] = (unsigned int)addr;
    if (dbc_call(0x80001304) < 0) {
        DPRINT(D_004B99A8, 0);
        return 0;
    }
    return (int)D_00679840[0];
}

// [C] not initialised: 0. Request 0x80001301: +0 / +4 / +8 / +0xC / +0x10 = the parameter's words (+0x28 = buf0 and
// +0x2C = buf1 stored between), bytes +0x14..+0x23 = the parameter's; returns the reply's +0x24 (the socket), or the
// message and 0.
// 0x00216268
extern "C" int sceDbcCreateSocket(unsigned int *param, void *buf0, void *buf1)
{
    unsigned char *src;
    unsigned char *dst;
    int i;

    if (isInit == 0) {
        return 0;
    }
    D_00679840[0] = param[0];
    D_00679840[10] = (unsigned int)buf0;
    D_00679840[11] = (unsigned int)buf1;
    D_00679840[1] = param[1];
    D_00679840[2] = param[2];
    D_00679840[3] = param[3];
    D_00679840[4] = param[4];
    src = (unsigned char *)param + 0x14;
    dst = (unsigned char *)D_00679840 + 0x14;
    for (i = 0; i < 0x10; i++) {
        dst[i] = src[i];
    }
    if (dbc_call(0x80001301) >= 0) {
        return (int)D_00679840[9];
    }
    DPRINT(D_004B99C8, 0);
    return 0;
}

// [C] not initialised: 0. Request 0x80001302 with +0 = the socket; returns the reply's +4, or the message and 0.
// 0x00216340
extern "C" int sceDbcDeleteSocket(int socket)
{
    if (isInit == 0) {
        return 0;
    }
    D_00679840[0] = (unsigned int)socket;
    if (dbc_call(0x80001302) >= 0) {
        return (int)D_00679840[1];
    }
    DPRINT(D_004B99E8, 0);
    return 0;
}

// [C] the status words copied; the socket's != 1: -12 (no request). Else request 0x80001303 with +0 = the socket;
// returns the reply's +4, or the message and 0.
// 0x002163C8
extern "C" int sceDbcGetDepNumber(int socket)
{
    dbc_copy_status();
    if (D_0067CD40[socket] != 1) {
        return -12;
    }
    D_00679840[0] = (unsigned int)socket;
    if (dbc_call(0x80001303) >= 0) {
        return (int)D_00679840[1];
    }
    DPRINT(D_004B9A08, 0);
    return 0;
}

// [C] the status words copied; 1 when the socket's is 1, else 3. No request.
// 0x00216510
extern "C" int sceDbcGetConnection(int socket)
{
    dbc_copy_status();
    if (D_0067CD40[socket] == 1) {
        return 1;
    }
    return 3;
}

// [C] request 0x80001315 with +0 = the socket; the reply's +4, or the message and 0.
// 0x00216608
extern "C" int sceDbcInitSocket(int socket)
{
    D_00679840[0] = (unsigned int)socket;
    if (dbc_call(0x80001315) >= 0) {
        return (int)D_00679840[1];
    }
    DPRINT(D_004B9A28, 0);
    return 0;
}

// [C] request 0x80001316 with +0 = the socket; the reply's +4, or the message and 0.
// 0x00216678
extern "C" int sceDbcResetSocket(int socket)
{
    D_00679840[0] = (unsigned int)socket;
    if (dbc_call(0x80001316) >= 0) {
        return (int)D_00679840[1];
    }
    DPRINT(D_004B9A48, 0);
    return 0;
}

// [C] request 0x80001317 with +0 = the socket; the reply's +4, or the message and 0.
// 0x002166E8
extern "C" int sceDbcGetDeviceStatus(int socket)
{
    D_00679840[0] = (unsigned int)socket;
    if (dbc_call(0x80001317) >= 0) {
        return (int)D_00679840[1];
    }
    DPRINT(D_004B9A68, 0);
    return 0;
}

// [C] request 0x8000131D with +8 = 2, +0x10 / +0x14 = the parameter's +8 / +0xC; the reply's +4, or the message (the
// same string as sceDbcGetDeviceStatus') and -1.
// 0x00216758
extern "C" int sceDbcSearchPortSpecific(unsigned int *param)
{
    D_00679840[2] = 2;
    D_00679840[4] = param[2];
    D_00679840[5] = param[3];
    if (dbc_call(0x8000131D) >= 0) {
        return (int)D_00679840[1];
    }
    DPRINT(D_004B9A68, 0);
    return -1;
}

// [C] request 0x80001318: +0 = a0, +4 = a1, +8 = *sendSize, +0xC = *recvSize, *sendSize bytes from src at +0x10 (the
// count re-read each byte). A negative call: the message, 0. The reply's +0x410 < 0: returned. Else *sendSize = +8,
// *recvSize = +0xC, +0xC bytes from +0x210 to dst; returns +0x410.
// 0x002167E0
extern "C" int sceDbcSRData(int socket, unsigned int a1, int *sendSize, unsigned char *src, int *recvSize,
                            unsigned char *dst)
{
    unsigned char *b;
    int r;
    int i;

    b = (unsigned char *)D_00679840;
    D_00679840[0] = (unsigned int)socket;
    D_00679840[1] = a1;
    D_00679840[2] = (unsigned int)*sendSize;
    D_00679840[3] = (unsigned int)*recvSize;
    for (i = 0; i < *sendSize; i++) {
        b[0x10 + i] = src[i];
    }
    if (dbc_call(0x80001318) < 0) {
        DPRINT(D_004B9A90, 0);
        return 0;
    }
    r = (int)D_00679840[0x410 / 4];
    if (r < 0) {
        return r;
    }
    *sendSize = (int)D_00679840[2];
    *recvSize = (int)D_00679840[3];
    for (i = 0; i < (int)D_00679840[3]; i++) {
        dst[i] = b[0x210 + i];
    }
    return (int)D_00679840[0x410 / 4];
}

// [C] WaitSema(D_0067CD80); request 0x80001319: +0 = a0, +4 = a1, +8 = *size, *size bytes from src at +0xC. A
// negative call: the message, SignalSema, 0. Else when the reply's +0x20C >= 0: *size = +8; SignalSema; returns +0x20C.
// 0x00216928
extern "C" int sceDbcSendData(int socket, unsigned int a1, int *size, unsigned char *src)
{
    unsigned char *b;
    int i;

    WaitSema(D_0067CD80);
    b = (unsigned char *)D_00679840;
    D_00679840[0] = (unsigned int)socket;
    D_00679840[1] = a1;
    D_00679840[2] = (unsigned int)*size;
    for (i = 0; i < *size; i++) {
        b[0xC + i] = src[i];
    }
    if (dbc_call(0x80001319) < 0) {
        DPRINT(D_004B9AB0, 0);
        SignalSema(D_0067CD80);
        return 0;
    }
    if ((int)D_00679840[0x20C / 4] >= 0) {
        *size = (int)D_00679840[2];
    }
    SignalSema(D_0067CD80);
    return (int)D_00679840[0x20C / 4];
}

// [C] *size > 0x1080: the message (with the size), 0. WaitSema; D_00679AC0: +0 = a0, +4 = a1, +8 = *size, the bytes
// at +0xC; the 0x8000131B client still busy (sceSifCheckStatRpc == 1): the message, SignalSema, 0. Request 0x8000131B
// on it, no wait (mode 1), 0x1090 both ways; negative: the message, SignalSema, 0. Else SignalSema, 1.
// 0x00216A58
extern "C" int sceDbcSendData2(int socket, unsigned int a1, int *size, unsigned char *src)
{
    unsigned char *b;
    int i;

    if (*size > 0x1080) {
        DPRINT(D_004B9AD0, (unsigned int)*size);
        return 0;
    }
    WaitSema(D_0067CD80);
    b = (unsigned char *)D_00679AC0;
    D_00679AC0[0] = (unsigned int)socket;
    D_00679AC0[1] = a1;
    D_00679AC0[2] = (unsigned int)*size;
    for (i = 0; i < *size; i++) {
        b[0xC + i] = src[i];
    }
    if (sceSifCheckStatRpc(&D_006797F0) == 1) {
        DPRINT(D_004B9B00, 0);
        SignalSema(D_0067CD80);
        return 0;
    }
    if (sceSifCallRpc(&D_006797F0, 0x8000131B, 1, D_00679AC0, 0x1090, D_00679AC0, 0x1090, 0, 0) < 0) {
        DPRINT(D_004B9B20, 0);
        SignalSema(D_0067CD80);
        return 0;
    }
    SignalSema(D_0067CD80);
    return 1;
}

// [C] *size > 0x2080: the message, 0. WaitSema; D_0067ABC0: +0 = a0, +4 = a1, +8 = *size, the bytes at +0xC;
// client 0x8000131C still busy: the message, SignalSema, 0. Request 0x8000131C on it, no wait, 0x2090 both ways, the
// caller's end function (its argument 0); negative: SignalSema, 0 (no message). Else SignalSema, 1.
// 0x00216BB8
extern "C" int sceDbcSendData3(int socket, unsigned int a1, int *size, unsigned char *src, unsigned int endfunc)
{
    unsigned char *b;
    int i;

    if (*size > 0x2080) {
        DPRINT(D_004B9B40, 0);
        return 0;
    }
    WaitSema(D_0067CD80);
    b = (unsigned char *)D_0067ABC0;
    D_0067ABC0[0] = (unsigned int)socket;
    D_0067ABC0[1] = a1;
    D_0067ABC0[2] = (unsigned int)*size;
    for (i = 0; i < *size; i++) {
        b[0xC + i] = src[i];
    }
    if (sceSifCheckStatRpc(&D_00679818) == 1) {
        DPRINT(D_004B9B68, 0);
        SignalSema(D_0067CD80);
        return 0;
    }
    if (sceSifCallRpc(&D_00679818, 0x8000131C, 1, D_0067ABC0, 0x2090, D_0067ABC0, 0x2090, endfunc, 0) < 0) {
        SignalSema(D_0067CD80);
        return 0;
    }
    SignalSema(D_0067CD80);
    return 1;
}

// [C] request 0x8000131A: +0 = a0, +4 = a1 (libpad2 passes a packed 4-byte descriptor), +8 = *size. A negative call:
// the message, 0. The reply's +0x20C < 0: returned. Else *size = +8, +8 bytes from +0xC to dst; returns +0x20C.
// 0x00216D20
extern "C" int sceDbcReceiveData(int socket, unsigned int a1, int *size, unsigned char *dst)
{
    unsigned char *b;
    int r;
    int i;

    b = (unsigned char *)D_00679840;
    D_00679840[0] = (unsigned int)socket;
    D_00679840[1] = a1;
    D_00679840[2] = (unsigned int)*size;
    if (dbc_call(0x8000131A) < 0) {
        DPRINT(D_004B9B88, 0);
        return 0;
    }
    r = (int)D_00679840[0x20C / 4];
    if (r < 0) {
        return r;
    }
    *size = (int)D_00679840[2];
    for (i = 0; i < (int)D_00679840[2]; i++) {
        dst[i] = b[0xC + i];
    }
    return (int)D_00679840[0x20C / 4];
}

// [C] a disabled debug print: saves a1..t3 into its own frame's argument area and returns (varargs in the asm; here
// the one extra argument any caller passes, a1; the other registers' spill is stack only).
// 0x00216E10
extern "C" void DPRINT(const char *fmt, unsigned int a1)
{
}
