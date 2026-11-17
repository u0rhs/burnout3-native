# System files: the program, IOP modules, the network GUI, the demo, icons

The disc also carries files that are not game content: the PS2 program and its start file, the I/O processor (IOP)
modules, Sony's network set-up program, a playable demo of another game, and memory card icons. The port does not run
any PS2 program from the disc (it runs the game's code natively, [DESIGN.md](../DESIGN.md) §0), so most of these files
are only read for a check or not read at all. None of them is a modding target; the first two cannot be replaced by a
mod (the loader refuses them, [getting-started.md](../getting-started.md)).

Evidence tags: see [index.md](index.md#evidence-tags).

| Path | What it is | The port | Mod status |
|---|---|---|---|
| `SLUS_210.50` | the game's PS2 program (ELF, 4,073,816 bytes) | read: accepted only as the retail ELF (size + FNV-1a 64); its data image is loaded, its code never runs [P] | not replaceable (refused by the loader) |
| `SYSTEM.CNF` | the PS2 start file: `BOOT2 = cdrom0:\SLUS_210.50;1`, `VER`, `VMODE = NTSC` [D] | the launcher checks that it is there (src/b3launcher/core.cpp) [P] | not replaceable (refused by the loader) |
| `CNF/ICON.SYS`, `CNF/SYS_NET.ICO` | memory card icon pair for a saved network configuration (SYS_NET.ICO equals IOP/SYS_NET.ICO, ICON.SYS differs from IOP/ICON.SYS) [D] [L: used by the network set-up] | not used | not a target |
| `IOP/*.IRX` (18 modules) | IOP modules the game loads: RWA.IRX (RenderWare Audio), LIBSD.IRX (Sony sound library), B3ROUTE.IRX (Criterion's sound routing), GTFSCDVD / GTFSHOST / GTFSATMN.IRX (file systems), SIO2MAN / SIO2D / DBCMAN / DS2O.IRX (controllers), MC2_S1 / MCMAN / MCSERV.IRX (memory card), USBD / LGDEVW / LGAUD / LGKBM.IRX (USB: Logitech wheel, headset, keyboard), ATTYMON.IRX (a debug console [G]) [C: the module names in the program's strings] | none of them runs. Their work is done natively: RWA.IRX + LIBSD (src/audio/rwa_iop.cpp, spu2_mixer_impl.cpp), B3ROUTE.IRX (src/audio/b3route.cpp), the file system (src/native/platform_gtfs.cpp), the devices (platform_devices.cpp). **LIBSD.IRX is read at run time**: its reverb presets (Sony data, never in the repo) are taken from the user's file (src/native/platform_rwa.cpp) [P] | not a target (a changed LIBSD.IRX changes the reverb presets the port reads; the presets are checked by hash) |
| `IOP/IOPRP280.IMG`, `IOP/DNAS280.IMG` | IOP replacement image (newer IOP modules for the kernel) and the DNAS online authentication image | the IOPRP name is logged only (src/native/platform_iop.cpp) [P] | not a target |
| `IOP/ICON.SYS`, `IOP/SYS_NET.ICO` | the icon pair the network library names (`iconsys=cdrom0:\IOP\ICON.SYS`, `icon=cdrom0:\IOP\SYS_NET.ICO`) [C program strings] | not used | not a target |
| `IOP/NETWORK/*.IRX` (14 modules) | the network stack: DEV9, SMAP (Ethernet), PPP / PPPOE, INET, NETCNF, DRTYSCKF (EA's socket library) ... | not run; SMAP.IRX's load is answered with -1 (no network adapter, src/native/platform_sif.cpp) [P] | not a target |
| `NETGUI/` (NTGUI_UC.ELF, MODULES/*.IRX, BG*.RGB, EFF.BD / EFF.HD, EUROPEAN.PF, JIS2UCS.BIN / UCS2JIS.BIN) | Sony's network configuration program (its own ELF, IOP modules, backgrounds, sound bank, font, character tables) [C: the program names `cdrom0:\NETGUI\NTGUI_UC.ELF;1`] | not run | not a target |
| `NFSUNDER/` (NFSUNDER.ELF, IOPRP280.IMG, IRX281A.BUN / IRX281B.BUN, ZDIR.BIN, ZZDATA0..3.BIN, 300 MB) | the playable demo of Need for Speed Underground 2 (EXTRAS > PLAY NFS:UNDERGROUND 2 DEMO) | the game hands over to it with `LoadExecPS2`; the port ends the run there (src/native/platform.cpp, race_check `SCENARIO=nfsdemo`) [P] | not a target |
| `DATA/SAVEICON.ICN` (15,234 bytes) | the PS2 3D icon of the game's save (PS2 icon format: magic 0x00010000, shape count, texture type, 1.0f, vertex count, ...) [D header] | loaded by the game's code at boot into a 0x4000-byte buffer (`func_00133190` state 14) [C] and written to the memory card the port keeps as `view.ico` [L src/native/save_formats.h] | replaceable whole (cosmetic: the icon of new saves); must stay within 0x4000 bytes |
| `DATA/EALOGIN.ICO` (10,606 bytes) | the same icon format; loaded at boot into a 0x3000-byte buffer (`func_00133190` state 11) [C] | as the game does | replaceable whole, within 0x3000 bytes [G: shown with the EA online login save] |

Pitfalls: a mod should never ship any of these files (they are Sony's, EA's or Criterion's program code);
`b3modtool verify --game <disc>` reports a mod file that equals a disc file.
