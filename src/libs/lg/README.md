# src/libs/lg: Logitech's EE USB libraries as C (boot2, 2026-11-06)

lgdev (USB wheels / force feedback), later lgcodec (the USB headset) and lgkbm (keyboard), as retail links them, written
from the retail asm (the decomp's assembly/asm/lg/*.s) in the decomp's source style: decomp2host PORT_TRACKS["lg"], a
line `// 0xADDR` before each function, units `lg_<file>`. They talk to Logitech's IOP modules through SIF RPC (server
0x046D046D for lgdev). The native platform answers the two the game reaches -- lgDevInit and lgDevEnumerate (no
device attached) -- at their entry points, with no RPC (src/native/platform_devices.cpp).
