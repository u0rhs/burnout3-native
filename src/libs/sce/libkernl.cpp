// Sony EE library libkernl: the non-syscall helpers the sky2 driver's start-up reaches (SDK 2.8.0 as retail links
// it). Written from the retail asm (decomp assembly/asm/sce/libkernl.s). Tags: [C] confirmed from the asm, [L], [G].
// DIntr (func_00114E60) / EIntr are function calls in the asm (their COP0 Status work is theirs), so they are
// called by name; INTC_STAT (0x1000F000) accesses are b3hw.
// boot2 (2026-11-05).

#include "b3hw/hw.h"

extern "C" int func_00114E60(void);     // [C] DIntr: returns 1 when interrupts were enabled
extern "C" int EIntr(void);

// [C] with interrupts off, clears INTC_STAT's VBLANK-start bit (write 4), restores interrupts; polls INTC_STAT
// until bit 2 is set; then clears it again the same way. Returns 0, or EIntr's result (a tail jump, 0x10C330) when
// interrupts were enabled at the second DIntr.
// 0x0010C2B0
extern "C" int VSync(void)
{
    int was;

    was = func_00114E60();
    b3hw_write32(0x1000F000, 4);
    // sync
    if (was != 0) {
        EIntr();
    }
    while ((b3hw_read32(0x1000F000) & 4) == 0) {
    }
    was = func_00114E60();
    b3hw_write32(0x1000F000, 4);
    // sync
    if (was != 0) {
        return EIntr();
    }
    return was;
}

// boot2 (2026-11-07): libkernl's top thread, created by InitThread from _InitSys (the platform makes it as retail does,
// platform.cpp init_sys): a request ring at `arg` (D_004E3648) served one entry per signal of D_004E3640 -- the index
// is arg[0] & 0x1FF (then arg[0] = index + 1), the entry the two bytes at arg + 8 + 2 * index: an op and a thread id;
// op 0 WakeupThread(id), 1 RotateThreadReadyQueue(id), 2 SuspendThread(id), else kprintf(D_004B2040) [C libkernl.s
// 0x10CB20]. Nothing signals it on the boot's paths so far: it waits in WaitSema (navproof14s1 poll 1: thread 2, wait 2
// 2).
extern "C" int WaitSema(int sema);
extern "C" int WakeupThread(int id);
extern "C" int RotateThreadReadyQueue(int prio);
extern "C" int SuspendThread(int id);
extern "C" int kprintf(const char *fmt, ...);
extern int D_004E3640;              // [C] 0x004E3640 (.bss): the top thread's semaphore id (InitThread)
extern char D_004B2040[];           // [C] 0x004B2040: kprintf's message for an unknown op
// 0x0010CB20
extern "C" void topThread(void *arg)
{
    unsigned char *q = (unsigned char *)arg;
    for (;;) {
        WaitSema(D_004E3640);
        const unsigned int i = *(unsigned int *)q & 0x1FF;
        *(unsigned int *)q = i + 1;
        const unsigned char op = q[8 + i * 2];
        const unsigned char id = q[9 + i * 2];
        if (op == 1) {
            RotateThreadReadyQueue(id);
        } else if (op < 2) {
            if (op == 0) WakeupThread(id);
            else kprintf(D_004B2040);
        } else if (op == 2) {
            SuspendThread(id);
        } else {
            kprintf(D_004B2040);
        }
    }
}
