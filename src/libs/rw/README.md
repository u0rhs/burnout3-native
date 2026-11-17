# src/libs/rw: RenderWare 3.6 core as C (boot1, the RW plan of 2026-11-05)

Behaviour-equivalent C for the RenderWare 3.6 functions retail links (0x1D8260-0x211C30), written from the retail asm
(the decomp's assembly/asm/rw/renderware.s) at retail 3.6 struct layouts, in the decomp's source style. Not counted in
the decomp. RW's own symbol names come from the decomp's tables; unnamed functions are `func_<addr>`, unknown fields
`field_XX`. The RW 3.1 source and 3.5 headers are a reading aid only (names, layouts, intent), never copied.

- `<file>.cpp`: one file per RW 3.6 source file (the census's grouping, docs/results/boot1-2026-11-05/rw/); each
  function preceded by a line `// 0xADDR` of its own, which is how tools/decomp2host finds it (PORT_TRACKS["rw"], the
  same mechanism as src/libs/rwa; generated units are named `rw_<file>`).
- `include/`: headers for layouts shared between files (none yet; each file declares the views it uses).
- Proof: per function natively against retail in r5900_mini (native_check on captures, rw_trace states), then shadow.
- The sky2 driver (RpSky*, _rwDMA*, VU1, GS) is not ported here: host stubs in src/native keep the state game code reads.
