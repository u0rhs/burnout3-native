// Logitech lgDev (liblgdev.a as retail links it): the EE side of the lgdev.irx IOP module for USB wheels / force
// feedback. Written from the retail asm (decomp assembly/asm/lg/lgdev.s, data asm/data/lg/lgdev.{data,rodata}.s).
// Tags: [C] confirmed from the asm, [L] likely, [G] guess.
// One SIF RPC client D_01ECDF00 bound to server 0x046D046D; every request is a function number (1..0x12) on it with
// the 0x240-byte buffer D_01ECDF40 as send and receive area (the reply's +0 is the result code). The synchronous
// functions wait for the previous request first (func_00444AD0(0, 0)) and call in mode 0; the lgDevA* functions
// refuse (8) while a request is pending, then call in mode 1 on the second buffer D_004B0440 with an end function
// that copies the result out of the buffer. Errors of the RPC call itself: 0x80000000; bad parameter: 0x80000004.
// Request buffer layout [C]: +0 result, +4 / +8 / +0x18 first arguments (device / handle / effect id), +0x58 / +0x5C /
// +0x60 further arguments (property, count or value, pointer or value), +0x70 the payload (0x18 bytes per device
// state, 0x3C bytes per force effect). boot2 (2026-11-06).

// [C] the libsif client (sceSifClientData, 0x28 bytes): the bind loop waits for +0x24 (the server) to become nonzero.
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

// [C] the semaphore parameter on func_004438E0's stack ([L] SemaParam): memset 0x18 bytes, +4 = 1, +8 = 1.
struct UnkStruct_SemaParam {
    int field_0;
    int field_4;    // [C] 1 (max)
    int field_8;    // [C] 1 (init)
    int field_C;
    unsigned int field_10;
    unsigned int field_14;
};

// [C] the object func_00443860 resets (0x208 bytes; [G] a per-device state).
struct UnkStruct_00443860 {
    unsigned int field_0;
    unsigned int field_4;
    unsigned char field_8;
    unsigned char field_9;
    unsigned char pad_A[0xE0 - 0xA];
    int field_E0;
    unsigned char pad_E4[0x1FC - 0xE4];
    unsigned int field_1FC;
    int field_200;
    unsigned char field_204;
};

extern unsigned int D_004B0400;                 // [C] 0x004B0400 (.data): 1 after lgDevPrepareForIOPReboot saved the state
extern unsigned int D_004B0404;                 // [C] 0x004B0404: D_01ECE300 | 0x20000000 (the uncached hint word), set by func_004438E0
extern int D_004B0408;                          // [C] 0x004B0408: the semaphore id (-1 until created)
extern char *D_004B040C[];                      // [C] 0x004B040C: two string pointers (D_004D2338, D_004D23D8), word 0 printed by init
extern unsigned int D_004B0440[];               // [C] 0x004B0440: the asynchronous request buffer (0x240 + the end function's target)
extern UnkStruct_SifClient D_01ECDF00;          // [C] 0x01ECDF00: the RPC client (heap, outside the image)
extern unsigned int D_01ECDF40[];               // [C] 0x01ECDF40: the synchronous request / receive buffer (0x240)
extern unsigned char D_01ECDFB0[];              // [C] 0x01ECDFB0 = D_01ECDF40 + 0x70: the payload area
extern unsigned char D_01ECE180[];              // [C] 0x01ECE180: the saved payload (0x160 bytes) across an IOP reboot
extern unsigned char D_01ECE300[];              // [C] 0x01ECE300: the 0x40-byte hint area the IOP writes (uncached alias in D_004B0404)
extern char D_004D2378[];
extern char D_004D23A8[];
extern char D_004D23F0[];
extern char D_004D23F8[];
extern char D_004D2418[];
extern char D_004D2430[];
extern char D_004D2450[];
extern char D_004D2478[];
extern char D_004D24C0[];
extern char D_004D24D8[];
extern char D_004D2500[];
extern char D_004D2530[];
extern char D_004D2560[];
extern char D_004D2598[];
extern char D_004D25C0[];
extern char D_004D25E8[];
extern char D_004D2608[];
extern char D_004D2628[];
extern char D_004D2660[];
extern char D_004D2690[];
extern char D_004D26C0[];
extern char D_004D26F0[];
extern char D_004D2710[];
extern char D_004D2740[];
extern char D_004D2770[];
extern char D_004D27A0[];
extern char D_004D27D0[];
extern char D_004D27F8[];
extern char D_004D2830[];
extern char D_004D2870[];
extern char D_004D28A0[];
extern char D_004D28D8[];
extern char D_004D2918[];
extern char D_004D2948[];
extern char D_004D2970[];
extern char D_004D29B0[];
extern char D_004D29E0[];
extern char D_004D2A10[];
extern char D_004D2A38[];
extern char D_004D2A78[];
extern char D_004D2AA8[];
extern char D_004D2AD8[];
extern char D_004D2B08[];
extern char D_004D2B38[];
extern char D_004D2B68[];
extern char D_004D2BA8[];
extern char D_004D2BD8[];
extern char D_004D2C08[];
extern char D_004D2C38[];
extern char D_004D2C68[];
extern char D_004D2CA8[];
extern char D_004D2CE0[];
extern char D_004D2D20[];
extern char D_004D2D68[];
extern char D_004D2DB0[];
extern char D_004D2E00[];
extern char D_004D2E40[];
extern char D_004D2E70[];
extern char D_004D2EB8[];
extern char D_004D2F08[];
extern char D_004D2F48[];

extern "C" int printf(const char *, ...);
extern "C" void sceSifInitRpc(unsigned int mode);
extern "C" int sceSifBindRpc(UnkStruct_SifClient *cd, unsigned int id, unsigned int mode);
extern "C" int sceSifCallRpc(UnkStruct_SifClient *cd, unsigned int fno, unsigned int mode, void *send, int ssize,
                             void *recv, int rsize, unsigned int endfunc, unsigned int endpara);
extern "C" int sceSifCheckStatRpc(UnkStruct_SifClient *cd);
extern "C" void InvalidDCache(void *start, void *end);
extern "C" void *memset(void *p, int c, unsigned int n);
extern "C" int CreateSema(UnkStruct_SemaParam *param);

extern "C" void func_00444B58(void *buf);
extern "C" void func_00444B90(void *buf);
extern "C" void func_00444C00(void *buf);
extern "C" void func_00443888(int version);
extern "C" int func_00444AD0(int async, unsigned int *status);

// [C] the ldl/ldr + sdl/sdr dword copies of the asm (any alignment): n bytes.
static inline void lg_copy(void *dst, const void *src, int n)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    int i;

    for (i = 0; i < n; i++) {
        d[i] = s[i];
    }
}

// [C] the synchronous request: client D_01ECDF00, function number fno, D_01ECDF40 both ways (0x240), waiting.
static inline int lg_call(unsigned int fno)
{
    return sceSifCallRpc(&D_01ECDF00, fno, 0, D_01ECDF40, 0x240, D_01ECDF40, 0x240, 0, 0);
}

// [C] the asynchronous request: D_004B0440 both ways, mode 1, the end function with the buffer as its argument.
static inline int lg_acall(unsigned int fno, void (*endfunc)(void *))
{
    return sceSifCallRpc(&D_01ECDF00, fno, 1, D_004B0440, 0x240, D_004B0440, 0x240, (unsigned int)endfunc, (unsigned int)D_004B0440);
}

// [C] the force effect parameter block (0x3C bytes) into the buffer's payload at +0x70 (ldl/ldr dwords at 0..0x37, lw 0x38).
static inline void lg_effect_in(unsigned char *buf, const unsigned char *src)
{
    lg_copy(buf + 0x70, src, 0x3C);
}

// [C] resets the object: +4 = 0, +0xE0 = -1, +0x1FC = 0, +0x200 = -1, byte +9 = 1, +8 = 0, +0x204 = 0.
// 0x00443860
extern "C" void func_00443860(UnkStruct_00443860 *p)
{
    p->field_4 = 0;
    p->field_E0 = -1;
    p->field_1FC = 0;
    p->field_200 = -1;
    p->field_9 = 1;
    p->field_8 = 0;
    p->field_204 = 0;
}

// [C] prints the module's version word: printf(D_004D2378, 1, 0xB, 0x1B) (the library's own version 1.11.27), then (tail
// call) printf(D_004D23A8, v >> 24 (arithmetic), (v >> 16) & 0xFF, (v >> 8) & 0xFF).
// 0x00443888
extern "C" void func_00443888(int version)
{
    printf(D_004D2378, 1, 0xB, 0x1B);
    printf(D_004D23A8, version >> 24, (version >> 16) & 0xFF, (version >> 8) & 0xFF);
}

// [C] lgDevInit: printf("%s\n", D_004B040C[0]); sceSifInitRpc(0); D_004B0404 = D_01ECE300 | 0x20000000 with 0 stored
// through it; the bind loop on 0x046D046D (negative: message, halt in a nop loop; client +0x24 zero: a 0x10001 iteration
// delay, retry); +4 = 0, InvalidDCache(D_01ECE300, +0x3F), +0x5C = D_01ECE300, request 0xC (nonzero: two messages,
// halt). The reply's +4 is the IOP module's version: 0x010B1B00 fine; 0x010B.... (major.minor equal): warning + version;
// else message, version, halt. D_004B0408 == -1: CreateSema {max 1, init 1} (-1: two messages, halt) else a message.
// D_004B0400 set: the saved 0x160 payload bytes copied back to D_01ECDFB0 and request 0x12 sent, D_004B0400 = 0.
// Returns 0.
// 0x004438E0
extern "C" int func_004438E0(void)
{
    UnkStruct_SemaParam p;
    int id;
    int d;
    int i;

    printf(D_004D23F0, D_004B040C[0]);
    sceSifInitRpc(0);
    D_004B0404 = (unsigned int)D_01ECE300 | 0x20000000;
    *(volatile unsigned int *)D_004B0404 = 0;
    for (;;) {
        if (sceSifBindRpc(&D_01ECDF00, 0x046D046D, 0) < 0) {
            printf(D_004D23F8);
            for (;;) {
            }
        }
        if (D_01ECDF00.field_24 != 0) {
            break;
        }
        for (d = 0xFFFF; d != -1; d--) { // [C] 0x4439A0: a busy delay (0x10000 iterations of nops)
        }
    }
    D_01ECDF40[1] = 0;
    InvalidDCache(D_01ECE300, D_01ECE300 + 0x3F);
    D_01ECDF40[0x5C / 4] = (unsigned int)D_01ECE300;
    if (lg_call(0xC) != 0) {
        printf(D_004D2418);
        printf(D_004D2430);
        for (;;) {
        }
    }
    if (D_01ECDF40[1] != 0x010B1B00) {
        if ((D_01ECDF40[1] & 0xFFFF0000) == 0x010B0000) {
            printf(D_004D2478);
            func_00443888((int)D_01ECDF40[1]);
            printf(D_004D24C0);
        } else {
            printf(D_004D2450);
            func_00443888((int)D_01ECDF40[1]);
            printf(D_004D2430);
            for (;;) {
            }
        }
    }
    if (D_004B0408 == -1) {
        memset(&p, 0, 0x18);
        p.field_4 = 1;
        p.field_8 = 1;
        p.field_14 = 0;
        id = CreateSema(&p);
        D_004B0408 = id;
        if (id == -1) {
            printf(D_004D24D8);
            printf(D_004D2430);
            for (;;) {
            }
        }
    } else {
        printf(D_004D2500);
    }
    if (D_004B0400 != 0) {
        for (i = 0; i < 0x160; i += 0x20) { // [C] 0x443B8C / 0x443BE8: 0xB blocks of 0x20 bytes
            lg_copy(D_01ECDFB0 + i, D_01ECE180 + i, 0x20);
        }
        lg_call(0x12);
        D_004B0400 = 0;
    }
    return 0;
}

// [C] a1 (the hint) null: message, 0x80000004. Else *a0 = the word at the uncached alias D_004B0404, which is then cleared;
// returns 0.
// 0x00443C70
extern "C" int lgDevEnumHint(unsigned int *hint)
{
    if (hint == 0) {
        printf(D_004D2530);
        return 0x80000004;
    }
    *hint = *(volatile unsigned int *)D_004B0404;
    *(volatile unsigned int *)D_004B0404 = 0;
    return 0;
}

// [C] request 1: +4 = a0; out null: message, 0x80000004. Reply: 0x18 bytes at +0x70 to out; returns the reply's +0.
// 0x00443CC8
extern "C" int lgDevEnumerate(unsigned int index, unsigned char *out)
{
    if (out == 0) {
        printf(D_004D2560);
        return 0x80000004;
    }
    func_00444AD0(0, 0);
    D_01ECDF40[1] = index;
    if (lg_call(1) != 0) {
        printf(D_004D2598);
        return 0x80000000;
    }
    lg_copy(out, (unsigned char *)D_01ECDF40 + 0x70, 0x18);
    return (int)D_01ECDF40[0];
}

// [C] request 2: +4 = a0; handle out null: message, 0x80000004. Reply: *out = +8; returns +0.
// 0x00443DB0
extern "C" int lgDevOpen(unsigned int device, unsigned int *handle)
{
    if (handle == 0) {
        printf(D_004D25C0);
        return 0x80000004;
    }
    func_00444AD0(0, 0);
    D_01ECDF40[1] = device;
    if (lg_call(2) != 0) {
        printf(D_004D25E8);
        return 0x80000000;
    }
    *handle = D_01ECDF40[2];
    return (int)D_01ECDF40[0];
}

// [C] request 3: +8 = the handle; returns +0.
// 0x00443E70
extern "C" int lgDevClose(unsigned int handle)
{
    func_00444AD0(0, 0);
    D_01ECDF40[2] = handle;
    if (lg_call(3) != 0) {
        printf(D_004D2608);
        return 0x80000000;
    }
    return (int)D_01ECDF40[0];
}

// [C] request 4: +8 = handle, +0x58 = property; value out null: message, 0x80000004. Reply: *out = +0x5C; returns +0.
// 0x00443EF8
extern "C" int lgDevGetDeviceProperty(unsigned int handle, unsigned int property, unsigned int *value)
{
    if (value == 0) {
        printf(D_004D2628);
        return 0x80000004;
    }
    func_00444AD0(0, 0);
    D_01ECDF40[2] = handle;
    D_01ECDF40[0x58 / 4] = property;
    if (lg_call(4) != 0) {
        printf(D_004D2660);
        return 0x80000000;
    }
    *value = D_01ECDF40[0x5C / 4];
    return (int)D_01ECDF40[0];
}

// [C] request 5: +8 = handle, +0x58 = property, +0x5C = value; returns +0.
// 0x00443FC8
extern "C" int lgDevSetDeviceProperty(unsigned int handle, unsigned int property, unsigned int value)
{
    func_00444AD0(0, 0);
    D_01ECDF40[2] = handle;
    D_01ECDF40[0x58 / 4] = property;
    D_01ECDF40[0x5C / 4] = value;
    if (lg_call(5) != 0) {
        printf(D_004D2690);
        return 0x80000000;
    }
    return (int)D_01ECDF40[0];
}

// [C] request 6: +8 = handle, +0x60 = a1; out null: message, 0x80000004. Reply: 0x18 bytes at +0x70 to out; returns +0.
// 0x00444070
extern "C" int lgDevRead(unsigned int handle, unsigned int a1, unsigned char *out)
{
    if (out == 0) {
        printf(D_004D26C0);
        return 0x80000004;
    }
    func_00444AD0(0, 0);
    D_01ECDF40[2] = handle;
    D_01ECDF40[0x60 / 4] = a1;
    if (lg_call(6) != 0) {
        printf(D_004D26F0);
        return 0x80000000;
    }
    lg_copy(out, (unsigned char *)D_01ECDF40 + 0x70, 0x18);
    return (int)D_01ECDF40[0];
}

// [C] n - 1 >= 4 (unsigned): message with n; a1 null / a2 null / out null: its message; all 0x80000004. Else request 0xE:
// handles a1[i] to +8 + 4 i, values a2[i] to +0x60 + 4 i, +0x5C = n. Reply: n blocks of 0x18 bytes from +0x70 to out;
// returns +0.
// 0x00444168
extern "C" int lgDevReadMulti(int n, unsigned int *handles, unsigned int *values, unsigned char *out)
{
    int i;

    if ((unsigned int)(n - 1) >= 4) {
        printf(D_004D2710, n);
        return 0x80000004;
    }
    if (handles == 0) {
        printf(D_004D2740);
        return 0x80000004;
    }
    if (values == 0) {
        printf(D_004D2770);
        return 0x80000004;
    }
    if (out == 0) {
        printf(D_004D27A0);
        return 0x80000004;
    }
    func_00444AD0(0, 0);
    for (i = 0; i < n; i++) {
        D_01ECDF40[2 + i] = handles[i];
        D_01ECDF40[0x60 / 4 + i] = values[i];
    }
    D_01ECDF40[0x5C / 4] = (unsigned int)n;
    if (lg_call(0xE) != 0) {
        printf(D_004D27D0);
        return 0x80000000;
    }
    for (i = 0; i < n; i++) {
        lg_copy(out + 0x18 * i, (unsigned char *)D_01ECDF40 + 0x70 + 0x18 * i, 0x18);
    }
    return (int)D_01ECDF40[0];
}

// [C] request 7: +8 = handle, +0x18 = *effect, the 0x3C-byte parameter block at +0x70; effect id or block null:
// message, 0x80000004. Reply: *effect = +0x18; returns +0.
// 0x00444330
extern "C" int lgDevDownloadForceEffect(unsigned int handle, int *effect, unsigned char *params)
{
    if (effect == 0) {
        printf(D_004D27F8);
        return 0x80000004;
    }
    if (params == 0) {
        printf(D_004D2830);
        return 0x80000004;
    }
    func_00444AD0(0, 0);
    D_01ECDF40[2] = handle;
    D_01ECDF40[6] = (unsigned int)*effect;
    lg_effect_in((unsigned char *)D_01ECDF40, params);
    if (lg_call(7) != 0) {
        printf(D_004D2870);
        return 0x80000000;
    }
    *effect = (int)D_01ECDF40[6];
    return (int)D_01ECDF40[0];
}

// [C] request 0xD: +0x18 = effect id, the block at +0x70; id == -1 / block null: message, 0x80000004; returns +0.
// 0x00444490
extern "C" int lgDevUpdateForceEffect(int effect, unsigned char *params)
{
    if (effect == -1) {
        printf(D_004D28A0);
        return 0x80000004;
    }
    if (params == 0) {
        printf(D_004D28D8);
        return 0x80000004;
    }
    func_00444AD0(0, 0);
    D_01ECDF40[6] = (unsigned int)effect;
    lg_effect_in((unsigned char *)D_01ECDF40, params);
    if (lg_call(0xD) != 0) {
        printf(D_004D2918);
        return 0x80000000;
    }
    return (int)D_01ECDF40[0];
}

// [C] request 8: +0x18 = effect id; returns +0.
// 0x004445D8
extern "C" int lgDevStartForceEffect(int effect)
{
    func_00444AD0(0, 0);
    D_01ECDF40[6] = (unsigned int)effect;
    if (lg_call(8) != 0) {
        printf(D_004D2948);
        return 0x80000000;
    }
    return (int)D_01ECDF40[0];
}

// [C] n - 1 >= 0x10 (unsigned): message with n, else ids null: message; 0x80000004. Request 0xF: ids[i] to +0x18 + 4 i,
// +0x5C = n; returns +0.
// 0x00444660
extern "C" int lgDevStartForceEffectMulti(int n, int *effects)
{
    int i;

    if ((unsigned int)(n - 1) >= 0x10) {
        printf(D_004D2970, n);
        return 0x80000004;
    }
    if (effects == 0) {
        printf(D_004D29B0);
        return 0x80000004;
    }
    func_00444AD0(0, 0);
    for (i = 0; i < n; i++) {
        D_01ECDF40[6 + i] = (unsigned int)effects[i];
    }
    D_01ECDF40[0x5C / 4] = (unsigned int)n;
    if (lg_call(0xF) != 0) {
        printf(D_004D29E0);
        return 0x80000000;
    }
    return (int)D_01ECDF40[0];
}

// [C] request 9: +0x18 = effect id; returns +0.
// 0x00444770
extern "C" int lgDevStopForceEffect(int effect)
{
    func_00444AD0(0, 0);
    D_01ECDF40[6] = (unsigned int)effect;
    if (lg_call(9) != 0) {
        printf(D_004D2A10);
        return 0x80000000;
    }
    return (int)D_01ECDF40[0];
}

// [C] like lgDevStartForceEffectMulti with request 0x10 and its own messages.
// 0x004447F8
extern "C" int lgDevStopForceEffectMulti(int n, int *effects)
{
    int i;

    if ((unsigned int)(n - 1) >= 0x10) {
        printf(D_004D2A38, n);
        return 0x80000004;
    }
    if (effects == 0) {
        printf(D_004D2A78);
        return 0x80000004;
    }
    func_00444AD0(0, 0);
    for (i = 0; i < n; i++) {
        D_01ECDF40[6 + i] = (unsigned int)effects[i];
    }
    D_01ECDF40[0x5C / 4] = (unsigned int)n;
    if (lg_call(0x10) != 0) {
        printf(D_004D2AA8);
        return 0x80000000;
    }
    return (int)D_01ECDF40[0];
}

// [C] request 0xA: +0x18 = effect id; returns +0.
// 0x00444908
extern "C" int lgDevDestroyForceEffect(int effect)
{
    func_00444AD0(0, 0);
    D_01ECDF40[6] = (unsigned int)effect;
    if (lg_call(0xA) != 0) {
        printf(D_004D2AD8);
        return 0x80000000;
    }
    return (int)D_01ECDF40[0];
}

// [C] request 0x11; on success the 0x160 payload bytes at +0x70 are saved to D_01ECE180 and D_004B0400 = 1, returns +0; else
// D_004B0400 = 0, message, 0x80000000 (func_004438E0 restores them after the IOP is rebooted).
// 0x00444990
extern "C" int lgDevPrepareForIOPReboot(void)
{
    int i;

    func_00444AD0(0, 0);
    if (lg_call(0x11) != 0) {
        D_004B0400 = 0;
        printf(D_004D2B08);
        return 0x80000000;
    }
    for (i = 0; i < 0x160; i += 0x20) { // [C] 0x444A04 / 0x444A60: 0xB blocks of 0x20 bytes
        lg_copy(D_01ECE180 + i, (unsigned char *)D_01ECDF40 + 0x70 + i, 0x20);
    }
    D_004B0400 = 1;
    return (int)D_01ECDF40[0];
}

// [C] waits for the RPC client to be idle. async == 0: loops on sceSifCheckStatRpc(&D_01ECDF00) until it returns 0, then
// (status non-null) *status = D_004B0440[0]; returns 0. async != 0: one check; nonzero: returned (busy); else *status =
// D_004B0440[0] when non-null; returns 0.
// 0x00444AD0
extern "C" int func_00444AD0(int async, unsigned int *status)
{
    int r;

    if (async == 0) {
        while (sceSifCheckStatRpc(&D_01ECDF00) != 0) {
        }
        if (status != 0) {
            *status = D_004B0440[0];
        }
        return 0;
    }
    r = sceSifCheckStatRpc(&D_01ECDF00);
    if (r != 0) {
        return r;
    }
    if (status != 0) {
        *status = D_004B0440[0];
    }
    return r;
}

// [C] an asynchronous read's end function (D_004B0440 as the argument): 0x18 bytes at +0x70 to the pointer stored at +0x240.
// 0x00444B58
extern "C" void func_00444B58(void *buf)
{
    unsigned char *b = (unsigned char *)buf;

    lg_copy(*(unsigned char **)(b + 0x240), b + 0x70, 0x18);
}

// [C] lgDevAReadMulti's end function: +0x5C blocks of 0x18 bytes from +0x70 to the pointer at +0x240 (the count re-read each
// iteration).
// 0x00444B90
extern "C" void func_00444B90(void *buf)
{
    unsigned char *b = (unsigned char *)buf;
    int i;

    for (i = 0; i < *(int *)(b + 0x5C); i++) {
        lg_copy(*(unsigned char **)(b + 0x240) + 0x18 * i, b + 0x70 + 0x18 * i, 0x18);
    }
}

// [C] an empty end function.
// 0x00444C00
extern "C" void func_00444C00(void *buf)
{
}

// [C] async request 6: out null: message, 0x80000004. func_00444AD0(1, 0) != 0 (a request pending): message, 0x80000008.
// Else D_004B0440: +8 = handle, +0x60 = a1, +0x240 = out; request 6 with func_00444B58; returns 0.
// 0x00444C08
extern "C" int lgDevARead(unsigned int handle, unsigned int a1, unsigned char *out)
{
    if (out == 0) {
        printf(D_004D2B38);
        return 0x80000004;
    }
    if (func_00444AD0(1, 0) != 0) {
        printf(D_004D2B68);
        return 0x80000008;
    }
    D_004B0440[2] = handle;
    D_004B0440[0x60 / 4] = a1;
    D_004B0440[0x240 / 4] = (unsigned int)out;
    lg_acall(6, func_00444B58);
    return 0;
}

// [C] like lgDevReadMulti's checks (n - 1 >= 4, three null pointers), then the pending check (0x80000008), then
// request 0xE (asynchronous) with func_00444B90; +0x240 = out.
// 0x00444CD8
extern "C" int lgDevAReadMulti(int n, unsigned int *handles, unsigned int *values, unsigned char *out)
{
    int i;

    if ((unsigned int)(n - 1) >= 4) {
        printf(D_004D2BA8, n);
        return 0x80000004;
    }
    if (handles == 0) {
        printf(D_004D2BD8);
        return 0x80000004;
    }
    if (values == 0) {
        printf(D_004D2C08);
        return 0x80000004;
    }
    if (out == 0) {
        printf(D_004D2C38);
        return 0x80000004;
    }
    if (func_00444AD0(1, 0) != 0) {
        printf(D_004D2C68);
        return 0x80000008;
    }
    for (i = 0; i < n; i++) {
        D_004B0440[2 + i] = handles[i];
        D_004B0440[0x60 / 4 + i] = values[i];
    }
    D_004B0440[0x5C / 4] = (unsigned int)n;
    D_004B0440[0x240 / 4] = (unsigned int)out;
    lg_acall(0xE, func_00444B90);
    return 0;
}

// [C] async request 0xD (end function func_00444C00): effect == -1 / block null: message, 0x80000004; pending: message,
// 80000008; else +0x18 = effect, the block at +0x70, returns 0.
// 0x00444E48
extern "C" int lgDevAUpdateForceEffect(int effect, unsigned char *params)
{
    if (effect == -1) {
        printf(D_004D2CA8);
        return 0x80000004;
    }
    if (params == 0) {
        printf(D_004D2CE0);
        return 0x80000004;
    }
    if (func_00444AD0(1, 0) != 0) {
        printf(D_004D2D20);
        return 0x80000008;
    }
    D_004B0440[6] = (unsigned int)effect;
    lg_effect_in((unsigned char *)D_004B0440, params);
    lg_acall(0xD, func_00444C00);
    return 0;
}

// [C] async request 8: pending: message, 0x80000008; else +0x18 = effect, returns 0.
// 0x00444F98
extern "C" int lgDevAStartForceEffect(int effect)
{
    if (func_00444AD0(1, 0) != 0) {
        printf(D_004D2D68);
        return 0x80000008;
    }
    D_004B0440[6] = (unsigned int)effect;
    lg_acall(8, func_00444C00);
    return 0;
}

// [C] async request 0xF: the pending check first (message, 0x80000008), then n - 1 >= 0x10 (message with n) / ids null
// (message), 0x80000004; ids to +0x18 + 4 i, +0x5C = n; returns 0.
// 0x00445028
extern "C" int lgDevAStartForceEffectMulti(int n, int *effects)
{
    int i;

    if (func_00444AD0(1, 0) != 0) {
        printf(D_004D2DB0);
        return 0x80000008;
    }
    if ((unsigned int)(n - 1) >= 0x10) {
        printf(D_004D2E00, n);
        return 0x80000004;
    }
    if (effects == 0) {
        printf(D_004D2E40);
        return 0x80000004;
    }
    for (i = 0; i < n; i++) {
        D_004B0440[6 + i] = (unsigned int)effects[i];
    }
    D_004B0440[0x5C / 4] = (unsigned int)n;
    lg_acall(0xF, func_00444C00);
    return 0;
}

// [C] async request 9: pending: message, 0x80000008; else +0x18 = effect, returns 0.
// 0x00445148
extern "C" int lgDevAStopForceEffect(int effect)
{
    if (func_00444AD0(1, 0) != 0) {
        printf(D_004D2E70);
        return 0x80000008;
    }
    D_004B0440[6] = (unsigned int)effect;
    lg_acall(9, func_00444C00);
    return 0;
}

// [C] like lgDevAStartForceEffectMulti with request 0x10 and its own messages.
// 0x004451D8
extern "C" int lgDevAStopForceEffectMulti(int n, int *effects)
{
    int i;

    if (func_00444AD0(1, 0) != 0) {
        printf(D_004D2EB8);
        return 0x80000008;
    }
    if ((unsigned int)(n - 1) >= 0x10) {
        printf(D_004D2F08, n);
        return 0x80000004;
    }
    if (effects == 0) {
        printf(D_004D2F48);
        return 0x80000004;
    }
    for (i = 0; i < n; i++) {
        D_004B0440[6 + i] = (unsigned int)effects[i];
    }
    D_004B0440[0x5C / 4] = (unsigned int)n;
    lg_acall(0x10, func_00444C00);
    return 0;
}
