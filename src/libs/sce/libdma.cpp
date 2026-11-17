// Sony EE library libdma (SDK 2.8.0 as retail links it: the .data id string at 0x00483F60 reads "PsIIlibdma  2800").
// Written from the retail asm (decomp assembly/asm/sce/libdma.s, data asm/data/sce/libdma.{rodata,bss}.s and
// libmpeg.data.s for the channel table). Tags: [C] confirmed from the asm, [L] likely, [G] guess.
// A DMA channel ([L] sceDmaChan *) is the guest address of the channel's register block (0x10008000 ... 0x1000D400,
// the values of dch), so it is passed as an unsigned int: every access through it is a b3hw call at that address +
// the register's offset (CHCR +0, MADR +0x10, QWC +0x20, TADR +0x30, ASR0 +0x40, ASR1 +0x50, SADR +0x80). DMA
// addresses handed to the hardware (tags, buffers) are unsigned ints too: retail passes them through unchanged
// (CheckAddress only rewrites scratchpad addresses), whatever alias they use.
// boot2 (2026-11-05).

#include "b3hw/hw.h"

// [C] the DMA environment (0x14 bytes; [L] sceDmaEnv: sts, std, mfd, rcycle, express, notify, sqwc, tqwc, rbadr,
// rbmsk).
struct UnkStruct_sceDmaEnv {
    unsigned char field_0;      // [C] < 10: index into D_004B1AE0 -> D_CTRL bits 4-5
    unsigned char field_1;      // [C] < 10: index into stdtbl -> D_CTRL bits 6-7
    unsigned char field_2;      // [C] < 10: index into mfdtbl -> D_CTRL bits 2-3
    unsigned char field_3;      // [C] < 7: 0 clears D_CTRL bit 1, else bit 1 set and bits 8-9 = field_3 - 1
    unsigned short field_4;     // [C] D_PCR (0x1000E020) bits 16-31
    unsigned short field_6;     // [C] D_PCR bits 0-15
    unsigned short field_8;     // [C] D_SQWC (0x1000E030) bits 0-15
    unsigned short field_A;     // [C] D_SQWC bits 16-31
    unsigned int field_C;       // [C] -> D_RBOR (0x1000E050)
    unsigned int field_10;      // [C] -> D_RBSR (0x1000E040)
};

extern unsigned int dch[];                  // [C] 0x00483F70: the ten channels' register block addresses
extern unsigned int isclr[];                // [C] 0x004B1AB8: per channel, nonzero = cleared by sceDmaReset
extern unsigned char D_004B1AE0[];          // [C] 0x004B1AE0: D_CTRL bits 4-5 by env field_0 (read with lbu)
extern unsigned char stdtbl[];              // [C] 0x004B1AF0: D_CTRL bits 6-7 by env field_1 (lbu; table says u16)
extern unsigned char mfdtbl[];              // [C] 0x004B1B00: D_CTRL bits 2-3 by env field_2 (lbu; table says u16)
extern UnkStruct_sceDmaEnv D_004E3008;      // [C] 0x004E3008 (.bss, 0x38): the last environment put
extern char D_004B1AA0[];                   // [C] "libdma: sync timeout\n"

extern "C" int scePrintf(const char *fmt, ...);
extern "C" int func_00114E60(void);         // [C] DIntr: returns 1 when interrupts were enabled
extern "C" int EIntr(void);

extern "C" unsigned int sceDmaPause(unsigned int d);
extern "C" void memclr(void *p, int n);
extern "C" int sceDmaPutEnv(UnkStruct_sceDmaEnv *env);

// [C] while the channel's CHCR STR bit (0x100) is set: a counter from 0xFFFFFF counts down once per poll; once it is
// negative every poll first prints the timeout message and pauses the channel (sceDmaPause).
// 0x00103BC8
extern "C" void WaitDma(unsigned int d)
{
    int i;

    if (b3hw_read32(d) & 0x100) {
        i = 0xFFFFFF;
        do {
            if (i < 0) {
                scePrintf(D_004B1AA0);
                sceDmaPause(d);
            }
            i--;
        } while (b3hw_read32(d) & 0x100);
    }
}

// [C] a scratchpad address (top nibble 7) becomes (address & 0x0FFFFFFF) | 0x80000000 (SPR bit); others unchanged.
// 0x00103CC0
extern "C" unsigned int CheckAddress(unsigned int addr)
{
    if ((addr >> 28) == 7) {
        addr = (addr & 0x0FFFFFFF) | 0x80000000;
    }
    return addr;
}

// [C] clears n bytes (n == 0: none; the count is taken modulo 2^32).
// 0x00103CE8
extern "C" void memclr(void *p, int n)
{
    unsigned int left;
    char *b;

    b = (char *)p;
    left = (unsigned int)n;
    while (left != 0) {
        *b++ = 0;
        left--;
    }
}

// [C] the register block address of channel id (0-9), else 0.
// 0x00103D20
extern "C" unsigned int sceDmaGetChan(unsigned int id)
{
    if (id < 10) {
        return dch[id];
    }
    return 0;
}

// [C] reads D_CTRL (0x1000E000); for each channel isclr marks, clears SADR, CHCR, TADR, MADR, ASR1, ASR0 (that
// order); D_STAT (0x1000E010) = 0xFF1F (clears the status bits), then D_STAT &= 0xFF1F0000 (read, write); puts an
// all-zero environment; mode 1 sets D_CTRL bit 0 (DMAE, read then write). Returns the old D_CTRL bit 0.
// 0x00103D48
extern "C" int sceDmaReset(int mode)
{
    UnkStruct_sceDmaEnv env;
    unsigned int old;
    unsigned int ch;
    unsigned int v;
    int i;

    old = b3hw_read32(0x1000E000) & 1;
    for (i = 0; i < 10; i++) {
        if (isclr[i] != 0) {
            ch = dch[i];
            b3hw_write32(ch + 0x80, 0);
            b3hw_write32(ch + 0x0, 0);
            b3hw_write32(ch + 0x30, 0);
            b3hw_write32(ch + 0x10, 0);
            b3hw_write32(ch + 0x50, 0);
            b3hw_write32(ch + 0x40, 0);
        }
    }
    b3hw_write32(0x1000E010, 0xFF1F);
    v = b3hw_read32(0x1000E010);
    b3hw_write32(0x1000E010, v & 0xFF1F0000);
    memclr(&env, 0x14);
    sceDmaPutEnv(&env);
    if (mode == 1) {
        v = b3hw_read32(0x1000E000);
        b3hw_write32(0x1000E000, v | 1);
    }
    return (int)old;
}

// [C] reads D_CTRL, D_PCR, D_SQWC, D_RBOR, D_RBSR (in that order; only D_CTRL is used); checks the environment
// (field_0 / field_1 / field_2 < 10, field_3 < 7: else -1 / -2 / -3 / -4, nothing written); writes D_CTRL (its
// bits 1-7 and 8-9 from the tables / field_3), D_PCR, D_SQWC, D_RBOR, D_RBSR; copies the environment to
// D_004E3008. Returns 0.
// 0x00103E28
extern "C" int sceDmaPutEnv(UnkStruct_sceDmaEnv *env)
{
    unsigned int ctrl;

    ctrl = b3hw_read32(0x1000E000);
    b3hw_read32(0x1000E020);
    b3hw_read32(0x1000E030);
    b3hw_read32(0x1000E050);
    b3hw_read32(0x1000E040);
    if (env->field_0 >= 10) {
        return -1;
    }
    if (env->field_1 >= 10) {
        return -2;
    }
    if (env->field_2 >= 10) {
        return -3;
    }
    if (env->field_3 >= 7) {
        return -4;
    }
    ctrl = (ctrl & 0xFFFFFFCF) | ((unsigned int)D_004B1AE0[env->field_0] << 4);
    ctrl = (ctrl & 0xFFFFFF3F) | ((unsigned int)stdtbl[env->field_1] << 6);
    ctrl = (ctrl & 0xFFFFFFF3) | ((unsigned int)mfdtbl[env->field_2] << 2);
    if (env->field_3 != 0) {
        ctrl |= 2;
        ctrl = (ctrl & 0xFFFFFCFF) | ((unsigned int)(env->field_3 - 1) << 8);
    } else {
        ctrl &= 0xFFFFFFFD;
    }
    b3hw_write32(0x1000E000, ctrl);
    b3hw_write32(0x1000E020, ((unsigned int)env->field_4 << 16) | env->field_6);
    b3hw_write32(0x1000E030, ((unsigned int)env->field_A << 16) | env->field_8);
    b3hw_write32(0x1000E050, env->field_C);
    b3hw_write32(0x1000E040, env->field_10);
    D_004E3008 = *env;      // [C] 0x103FD0-0x103FF4: 8 + 8 + 4 bytes, all read before any is written
    return 0;
}

// [C] chain-mode send: the tag address through CheckAddress; WaitDma; TADR (+0x30) = it unless TADR reads
// 0xFFFFFFFF; QWC (+0x20) = 0; CHCR = (CHCR & ~0xC) | 0x105 (chain mode, TTE, STR). The CHCR read comes before the
// QWC write [C 0x10408C].
// 0x00104050
extern "C" void sceDmaSend(unsigned int d, unsigned int tag)
{
    unsigned int addr;
    unsigned int chcr;

    addr = CheckAddress(tag);
    WaitDma(d);
    if (b3hw_read32(d + 0x30) != 0xFFFFFFFF) {
        b3hw_write32(d + 0x30, addr);
    }
    chcr = b3hw_read32(d + 0x0);
    b3hw_write32(d + 0x20, 0);
    b3hw_write32(d + 0x0, (chcr & 0xFFFFFFF3) | 0x105);
}

// [C] normal-mode send of size qwords: the address through CheckAddress; WaitDma; MADR (+0x10) = it unless MADR
// reads 0xFFFFFFFF; QWC (+0x20) = size; CHCR = (CHCR & ~0xC) | 0x101 (normal mode, STR).
// 0x001040B8
extern "C" void sceDmaSendN(unsigned int d, unsigned int addr, int size)
{
    unsigned int a;
    unsigned int chcr;

    a = CheckAddress(addr);
    WaitDma(d);
    if (b3hw_read32(d + 0x10) != 0xFFFFFFFF) {
        b3hw_write32(d + 0x10, a);
    }
    chcr = b3hw_read32(d + 0x0);
    b3hw_write32(d + 0x20, size);
    b3hw_write32(d + 0x0, (chcr & 0xFFFFFFF3) | 0x101);
}

// boot2 [C libdma.s 0x1042F0]: mode 1 polls -- the channel's CHCR STR bit (CHCR >> 8 & 1, 1 = still running);
// any other mode waits (WaitDma; `timeout` unused) and returns 0. (play1: RenderAllBackdrops__14CB3StaticTrack.)
// 0x001042F0
extern "C" int sceDmaSync(unsigned int d, int mode, int timeout)
{
    (void)timeout;
    if (mode == 1) {
        return (b3hw_read32(d) >> 8) & 1;
    }
    WaitDma(d);
    return 0;
}

// [C] with interrupts off (DIntr): reads D_ENABLER (0x1000F520); if its bit 16 is clear, D_ENABLEW (0x1000F590) =
// it | 0x10000 (holds all channels); reads D_CTRL (unused); CHCR &= ~0x100 (STR off: read, write); D_ENABLEW = the
// value read from D_ENABLER; EIntr when DIntr reported them enabled. Returns the CHCR read.
// 0x00104360
extern "C" unsigned int sceDmaPause(unsigned int d)
{
    int was;
    unsigned int enabler;
    unsigned int chcr;

    was = func_00114E60();
    enabler = b3hw_read32(0x1000F520);
    if ((enabler & 0x10000) == 0) {
        b3hw_write32(0x1000F590, enabler | 0x10000);
    }
    b3hw_read32(0x1000E000);
    chcr = b3hw_read32(d + 0x0);
    b3hw_write32(d + 0x0, chcr & 0xFFFFFEFF);
    b3hw_write32(0x1000F590, enabler);
    if (was != 0) {
        EIntr();
    }
    return chcr;
}
