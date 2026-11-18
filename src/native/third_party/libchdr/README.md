# libchdr (CHD disc images)

The reader behind `.chd` disc images (`include/b3asset/discfs.h`). Compiled as one C unit by
`src/b3asset/chd_libchdr.c` (its settings are there) and linked wherever discfs is.

| | |
|---|---|
| Project | libchdr, https://github.com/rtissera/libchdr |
| Commit | `47d794151b2b64f6aba60304e6480f4f5a2bc24b` (2026-10-11, "Merge pull request #198 from 0x000NULL/fix-crafted-chd") |
| Archive | https://github.com/rtissera/libchdr/archive/47d794151b2b64f6aba60304e6480f4f5a2bc24b.tar.gz |
| Archive SHA-256 | `ee1131e748a2c7d7559b7054540ebe49637433901f92071560df52e369f7c7af` |
| Fetched | 2026-11-18 |

Unmodified copies, at the archive's own paths, of what reading needs and nothing else:

- `include/libchdr/*.h`, `src/*.c` and `src/*.h` (except `src/libchdr_flac_microflac.cpp` and `src/link.T`);
- `deps/lzma-26.02/`: `include/` and `src/` (the LZMA SDK decoder; its `Asm/` and CMake files are left out);
- `deps/miniz-3.1.2/miniz.c`, `miniz.h` (inflate only: compiled with `MINIZ_NO_DEFLATE_APIS`);
- `deps/zstd-1.5.7/zstddeclib.c`, `zstd.h`, `zstd_errors.h` (the single-file decoder).

Left out: CMake files, `tests/`, `contrib/`, `docs/`, `cmake/`, `unity.c` (it omits the AV codec that `libchdr_chd.c`
names; `chd_libchdr.c` stands in for it). The archive holds no binaries; nothing here runs at build time.

Not from the archive:

- `include/dr_libs/dr_flac.h`: a two-line stand-in that includes the port's own dr_flac (`../../../dr_flac.h`, v0.13.5;
  libchdr bundles v0.13.4), so one dr_flac is in the tree. `chd_libchdr.c` compiles it with every function `static`.
- The license texts: `LICENSE-libchdr.txt` (the archive's `LICENSE.txt`), `LICENSE-lzma.txt` (`deps/lzma-26.02/LICENSE`),
  `LICENSE-miniz.txt` (the MIT notice at the top of `miniz.c` and the public-domain statement at its end),
  `LICENSE-zstd.txt` (zstd's `LICENSE` at its tag v1.5.7, https://raw.githubusercontent.com/facebook/zstd/v1.5.7/LICENSE,
  SHA-256 `7055266497633c9025b777c78eb7235af13922117480ed5c674677adc381c9d8`; the archive's copy only names it).

| Component | License |
|---|---|
| libchdr (Romain Tisserand; MAME's CHD code by Aaron Giles) | BSD 3-clause, `LICENSE-libchdr.txt` |
| LZMA SDK decoder (Igor Pavlov) | public domain, `LICENSE-lzma.txt` |
| miniz (Rich Geldreich, RAD Game Tools, Valve) | MIT (or public domain), `LICENSE-miniz.txt` |
| zstd decoder (Meta Platforms) | BSD 3-clause, `LICENSE-zstd.txt` |
