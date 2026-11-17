// ostimer.o (librwacore.a) -- the timer: the EE's COP0 Count (hand-written `mfc0 $9`), read through b3hw_cop0_count
// (the platform's virtual EE time natively; docs/audio.md §5). Retail 0x293530-0x293578 [C E map]. Not ported:
// func_00293550 (mfc0 into v1, returns the caller's v0; never called). C port written from retail's code (audio1).
#include "rwa/rwacore.h"
#include "b3hw/hw.h"

extern unsigned long long D_004E2AE8;   // the timer's start value, stored as a doubleword [C 0x293574 sd]

// The timer's frequency: the EE clock, 294.912 MHz [C 0x293534 lui 0x1194].
// 0x00293530
extern "C" RwUInt32 RwaTimerGetFrequency(void)
{
    return 0x11940000;
}

// 0x00293540
extern "C" RwUInt32 RwaTimerGetValue(void)
{
    return b3hw_cop0_count();
}

// Records the current Count as the timer's start (zero-extended into a doubleword) and returns 1.
// 0x00293560
extern "C" RwBool func_00293560(void)
{
    D_004E2AE8 = b3hw_cop0_count();
    return 1;
}
