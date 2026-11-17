#include "b3hw/hw.h"

// RenderWare 3.6 sky2 driver: start-up of the PS2 paths (RW 3.1 file name: driver/sky2/baevent.c; reading aid only).
// Written from the retail asm (decomp assembly/asm/rw/renderware.s). Tags: [C] confirmed from the asm, [L], [G].

extern "C" void sceGsResetPath(void);
extern "C" int sceDmaReset(int mode);

// [C] resets VIF1, VIF0 and the GIF (FBRST / CTRL RST), then VU1 and VU0 (COP2 vi28 FBRST bits RS1 0x200, RS0 0x2,
// each a read-modify-write), then libgraph's path reset and libdma's reset (mode 1). The VU0 read comes before the
// GIF store in the asm (0x1F4630), so the trace has it there too. No defined result (v0 is sceDmaReset's).
// 0x001F4600
extern "C" void _rwSkyInitConfigSystem(void)
{
    unsigned int fbrst;

    b3hw_write32(0x10003C10, 1);    // VIF1_FBRST.RST
    b3hw_write32(0x10003810, 1);    // VIF0_FBRST.RST
    fbrst = b3hw_cop2_ctrl_read(28);
    b3hw_write32(0x10003000, 1);    // GIF_CTRL.RST
    b3hw_cop2_ctrl_write(28, fbrst | 0x200);
    fbrst = b3hw_cop2_ctrl_read(28);
    b3hw_cop2_ctrl_write(28, fbrst | 0x2);
    sceGsResetPath();
    sceDmaReset(1);
}
