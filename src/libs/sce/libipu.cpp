// Sony EE library libipu (SDK 2.8.0 as retail links it: the .data id string at 0x004840C0 reads "PsIIlibipu  2800").
// Written from the retail asm (decomp assembly/asm/sce/libipu.s, data asm/data/sce/libipu.data.s). Every hardware
// access is a b3hw_* call in the asm's order (include/b3hw/hw.h). IPU registers [C EE User's Manual 6.0 ch. 8.3]:
// IPU_CMD 0x10002000 (write: command; read: VDEC / FDEC result, bit 63 busy), IPU_CTRL 0x10002010 (bit 31 BUSY, bit 30
// RST), IPU_in_FIFO 0x10007010 (128-bit writes). The toIPU DMA channel's CHCR is 0x1000B400; D_ENABLER 0x1000F520 /
// D_ENABLEW 0x1000F590 (bit 16 suspends all DMA channels while CHCR is written). Tags: [C] confirmed from the asm.
// movie1 (2026-11-06).

#include "b3hw/hw.h"

extern unsigned int iqval[];   // [C] 0x004840D0, 0x50 bytes: the intra matrix (4 qwords), then one qword of 16s
extern unsigned int vqval[];   // [C] 0x00484120, 0x20 bytes: the default VQ CLUT (16 RGB16 entries)

extern "C" int func_00114E60(void);   // [C] DIntr: returns 1 when interrupts were enabled
extern "C" int EIntr(void);

// [C] mode 0: waits until IPU_CTRL.BUSY is clear, returns 0; mode 1: returns IPU_CTRL.BUSY (bit 31); any other mode
// returns 0 without touching the IPU. The second argument is unused.
// 0x0010B6C0
extern "C" int sceIpuSync(int mode, unsigned short timeout)
{
    if (mode == 0) {
        while ((int)b3hw_read32(0x10002010) < 0) {
        }
        return 0;
    }
    if (mode == 1) {
        return b3hw_read32(0x10002010) >> 31;
    }
    return 0;
}

// [C] with interrupts off: DMA suspended (D_ENABLEW = D_ENABLER | 0x10000), toIPU CHCR = chcr, DMA resumed
// (D_ENABLEW = D_ENABLER & ~0x10000); interrupts back on when they were on.
// 0x0010B728
extern "C" void func_0010B728(unsigned int chcr)
{
    int was;

    was = func_00114E60();
    b3hw_write32(0x1000F590, b3hw_read32(0x1000F520) | 0x10000);
    b3hw_write32(0x1000B400, chcr);
    b3hw_write32(0x1000F590, b3hw_read32(0x1000F520) & 0xFFFEFFFF);
    if (was) {
        EIntr();
    }
}

static inline void ipu_wait(void)
{
    while ((int)b3hw_read32(0x10002010) < 0) {
    }
}

static inline void ipu_fifo_put(const unsigned int *q)
{
    b3hw_write128(0x10007010, ((unsigned long long)q[1] << 32) | q[0], ((unsigned long long)q[3] << 32) | q[2]);
}

// [C] toIPU CHCR = 1 (DIR from memory, STR 0); IPU reset; BCLR 0; the intra matrix (iqval qwords 0-3) and four
// copies of the qword of 16s (iqval qword 4) into IPU_in_FIFO, SETIQ intra (0x50000000), SETIQ non-intra (0x58000000);
// the CLUT (vqval, 2 qwords), SETVQ (0x60000000); SETTH 0 / 0 (0x90000000); IPU reset; BCLR 0. Every command and reset
// is followed by a poll of IPU_CTRL until BUSY clears.
// 0x0010B7A0
extern "C" void sceIpuInit(void)
{
    func_0010B728(1);
    b3hw_write32(0x10002010, 0x40000000);
    ipu_wait();
    b3hw_write32(0x10002000, 0);
    ipu_wait();
    ipu_fifo_put(&iqval[0]);
    ipu_fifo_put(&iqval[4]);
    ipu_fifo_put(&iqval[8]);
    ipu_fifo_put(&iqval[12]);
    ipu_fifo_put(&iqval[16]);
    ipu_fifo_put(&iqval[16]);
    ipu_fifo_put(&iqval[16]);
    ipu_fifo_put(&iqval[16]);
    b3hw_write32(0x10002000, 0x50000000);
    ipu_wait();
    b3hw_write32(0x10002000, 0x58000000);
    ipu_wait();
    ipu_fifo_put(&vqval[0]);
    ipu_fifo_put(&vqval[4]);
    b3hw_write32(0x10002000, 0x60000000);
    ipu_wait();
    b3hw_write32(0x10002000, 0x90000000);
    ipu_wait();
    b3hw_write32(0x10002010, 0x40000000);
    ipu_wait();
    b3hw_write32(0x10002000, 0);
    ipu_wait();
}
