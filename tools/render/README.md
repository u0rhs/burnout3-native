# tools/render: native renderer foundation (docs/native-renderer.md §10, game shading §11)

| What | How |
|---|---|
| Decoders + tests | `make test` (includes `build/test_b3asset`; real-data checks run when `GAME=<install>/game` exists, default `$B3_WIN/perf/install/game`) |
| Dump tool (Linux) | `make build/b3assetdump`; `build/b3assetdump census <game>`, `... txd <file> [outdir]`, `... car <CARn.BGV> [outdir] [lod] [palette]`, `... track <route dir> [outdir]` |
| HD texture packs (§18) | `make build/b3hdkeys`; `build/b3hdkeys scan <pack dir> <file|dir>... [--matched out] [--keys out.tsv] [--list]` (PCSX2 keys of the game's rasters, pack hits), `... compare <pack dir> <file>...` (original vs replacement means), `... key <file> <offset hex>`, `... xxh <file> [len...]`; viewers: `--hd-pack <dir>` / `$B3_HD_PACK`, `--no-hd` |
| Viewer (Linux/WSL) | `make render`, then `tools/render/b3view.sh <file> [options]` (`--offscreen out.png` renders without a window) |
| Viewer (Windows) | `tools/render/build_win.sh` -> `%B3_WIN%\b3view\b3view.exe` (+ `SDL3.dll`, `b3assetdump.exe`) |
| Viewer (macOS) | `tools/mac/build.sh <disc folder>` then `tools/mac/b3view.sh AS/C1_V1` (docs/mac.md); or `make render` (on Darwin the SDL3 flags default to Homebrew's via pkg-config) |
| Shaders | `make shaders` (= `build_shaders.sh`): GLSL 450 -> SPIR-V + MSL into `src/b3gfx/shaders_gen.h` (committed) |
| Track format | `docs/track-format.md` (byte-level spec with decomp citations) |
| VU microprograms | `python3 -I tools/render/vudis.py <decomp>/assembly/assets/vu/game_XXXXXXXX.textbin.bin > build/vu/...txt` (listings are game-derived: keep them in `build/`) |
| VU1 reference checks | `build/b3vucheck check <game>/TRACKS` (track programs vs `trackshade`), `... dump <route> <type>`, `... stats <TRACKS>`; `build/b3vucheck_car <game>/PVEH/*/CAR*.B[GT]V`; `build/b3envdump <route> [outdir] [--vu <vu dir>]` (docs/native-renderer.md §11.4) |
| Census (Python) | `python3 -I tools/render/ps2_asset_census.py <dirs>` (render1's signature census) |

## Third-party pieces (outside the repo, versions pinned)

All unpacked without root into their own folders under `../ext/` (nothing executed from the packages except the
two shader compilers):

| Piece | Version | Source | sha256 |
|---|---|---|---|
| SDL3 (Linux) | 3.4.2+ds-1ubuntu1 | Ubuntu 26.04 `libsdl3-0`, `libsdl3-dev` (`apt-get download`, `dpkg -x` into `../ext/sdl3-3.4.2`), plus the runtime libraries it links that WSL lacks (libXss, libpulse, libdecor, sndfile, FLAC, vorbis, opus, ogg, mpg123, mp3lame, asyncns, apparmor) | `../ext/debs/SHA256SUMS` (libsdl3-0 `9cffe342...`, libsdl3-dev `95dc447d...`) |
| SDL3 (Windows) | 3.4.2-1 | MSYS2 `mingw-w64-x86_64-sdl3-3.4.2-1-any.pkg.tar.zst` (repo.msys2.org/mingw/mingw64), unpacked into `../ext/sdl3-3.4.2-mingw64` | `12a874d28420f436d6bb43edc69ce579b4d4c85ec5a239e5f1e8fe49fe5f615d` (checked by `build_win.sh`) |
| glslangValidator | 16.2.0 | Ubuntu `glslang-tools` -> `../ext/shadertools` | `2ad3ea81...` (SHA256SUMS) |
| spirv-cross | 1.4.335 | Ubuntu `spirv-cross`, `libspirv-cross-c-shared0` -> `../ext/shadertools` | `50d11b7e...`, `b89c39fc...` |
| Windows compiler | gcc 16.2 (MSYS2) | the verified toolchain `%B3_WIN%\toolchain\mingw64` | `tools/toolchain/toolchain.sha256` |

SDL3.dll (Windows) imports only system DLLs; `b3view.exe` is linked `-static`, so it needs only `SDL3.dll` beside it.

Reference reading only (never built or run): librw `../ext/librw/src` (MIT; `src/ps2/ps2raster.cpp` swizzle, the test
oracle in `tests/test_b3asset.cpp`).
