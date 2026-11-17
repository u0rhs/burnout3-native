# src/libs/sce: Sony EE library functions as C (boot2, 2026-11-05)

The pieces of Sony's EE libraries (SDK 2.8.0 as retail links it: libgraph, libdma, libkernl's non-syscall helpers)
that the sky2 driver reaches at start-up and per frame, written from the retail asm (the decomp's
assembly/asm/sce/*.s) in the decomp's source style, the same mechanism as src/libs/rw (decomp2host PORT_TRACKS["sce"],
a line `// 0xADDR` before each function, units `sce_<file>`). Every hardware access is a b3hw_* call
(include/b3hw/hw.h) in the asm's order with its width and value, so the b3hw trace can be compared with retail's MMIO
accesses in r5900_mini. Names are Sony's own symbols from the decomp's tables; unnamed ones `func_<addr>`.
Kernel system calls (SetGsCrt, GsPutIMR, AddDmacHandler, ...) stay in the native platform tier (src/native).
The SDK 3.0.3 sources are a reading aid for semantics only (they differ from 2.8.0 in places), never copied.
