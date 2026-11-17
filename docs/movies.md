# Movies in the native port: state and handoff (movie2, 2026-11-07)

The movie lane's handoff: what exists, what is proven, what is open. Branch `movie2`, head at the
commit that adds this file; it merges boot2 up to `dca43b6` (boot2 has moved on
since: `5d86af9` IOP time follows EE time, `05feb4c` libsif's RPC data, the `B3NATIVE_SIF_LOG` build -- merge boot2's
head first).

## 1. What exists [C]

| Piece | Where | Proof |
|---|---|---|
| Native IPU model (registers, FIFOs, toIPU / fromIPU DMA, FDEC / VDEC / BDEC / CSC, exact bitstream consumption) | `src/native/ipu_model.h`, `ipu_vlc.h`; since the device-state work dev-only: in `HwModelDev` (`hw_model_dev.h`) through `IpuHost`, the shipped game has no IPU (an access is fatal) | `test_ipu_model` 11 tests / 357 checks; `build/mpeg_play` (retail libmpeg in r5900_mini on the model): all 61 disc movies exact (30,252 pictures = their picture start codes, end flag, 0 MPEG errors) |
| Sony libmpeg + libipu as C (134 functions) | `src/libs/sce/libmpeg*.cpp`, `libipu.cpp`, `include/sce/mpeg_internal.h` | boot oracle: sceMpegGetPicture SAME at calls 10 / 200 / 1000 / 1700 (up to 343,847 RAM words, 65,602 hw accesses); 48 functions SAME at call 1; 83 not reached through dispatch |
| The boot plays the front-end video natively | `native_boot --movie-log [N]` (sceMpegIsEnd's picture counter / end with the VBlank) | Titles30.m2v 1709 pictures, end at VBlank 5522 (3418 after the first IsEnd), CGtVideoDecoder + libmpeg all native on boot2d (the decomp's feed fix 19f6f34fcc) |
| toSPR chain mode | `hw_model_dev.h` HwModelDev (dev-only since the device-state work; the shipped model refuses it) | libmpeg's dmaRefImage |
| Tools | `tools/native/port_unit_check.sh` (a port-track file generated + compiled in seconds); `B3NATIVE_IPU_LOG=N`; `B3NATIVE_LATCH_LOG=1`; the VBlank of polls 1-3 in `--poll-point` (was `--tickhash`, retired 2026-11-11) | |

Known gaps in the C (a reading of the code, unverified): `_csc_storeRefImage`'s > 0xFFFF-qword path, `_doCSC2`'s wait loop
relies on the fromIPU handler having run (it does: delayed DMAC completions are delivered), `_updateRefImage` with
picture structure 0, `_Error1`'s unset a3, `_motionComp0`'s division by +0x144 = 0. BDEC output had no IDCT until mac4 (2026-11-08): every native movie showed
solid green (all-zero YUV) on every platform, the Mac report included. Now BDEC dequantises and runs an integer IDCT
(ipu_model.h; test_ipu_model `ipu_idct_ieee1180`, `ipu_bdec_pixels`), and `mpeg_play --dump-picture N out.ppm` writes a
picture (with the scratchpad wired for libmpeg's reference fetches: before, its P / B pictures were zero in that tool
only). CRSHHD01.M2V picture 150 shows the "WATERFRONT TIMES / BAYSID" newspaper over the bay.

## 2. Other open items

- The IPU model has no IDCT; native-decoded movie pixels for the renderer are a separate item (docs/native-renderer.md).
