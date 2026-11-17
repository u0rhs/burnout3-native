# The code-mod SDK

Everything needed to build a code mod for the Burnout 3 PC port: one C header and three build templates. A code mod
is a shared library the game loads before it starts; it can change what the game's functions do (hooks), add rows to
PC SETTINGS, draw over the game, and read files. How it works and every function: [docs/modding/sdk-reference.md](../docs/modding/sdk-reference.md).
Step by step: [tutorial 03](../docs/modding/tutorials/03-a-settings-row.md) (a settings row) and
[tutorial 05](../docs/modding/tutorials/05-an-overlay.md) (drawing over the game).

| File | What |
|---|---|
| `include/b3mod_api.h` | the API: a C struct of function pointers, versioned (`B3MOD_API_VERSION`) |
| `Makefile` | builds a mod with GNU make: Linux, macOS, Windows in an MSYS2 or Git Bash shell, Windows from Linux (MinGW-w64) |
| `build_windows.cmd` | builds a mod on Windows with MinGW-w64 alone (cmd.exe, no make) |
| `CMakeLists.txt` | builds a mod with CMake on any of the three (MinGW-w64 or MSVC on Windows) |

## A mod's folder

```
my-mod/
    mod.ini          [mod] with api = 2, and [code] naming the library per system
    src/my_mod.c     the sources (.c and .cpp; every one in src/ is built)
    bin/             made by the build: bin/linux/my_mod.so, bin/windows/my_mod.dll, bin/macos/my_mod.dylib
    data/            (optional) files that stand in for the disc's, as in a data mod
```

`mod.ini`:

```ini
[mod]
id = my-mod
name = My mod
version = 1.0
api = 2

[code]
windows = bin/windows/my_mod.dll
linux = bin/linux/my_mod.so
macos = bin/macos/my_mod.dylib
gameplay = no          # yes when it changes physics, AI or scoring
```

The smallest source:

```c
#include "b3mod_api.h"

B3MOD_DECLARE_VERSION()

B3MOD_EXPORT int b3mod_main(const B3ModApi* api, B3ModSelf* self) {
    api->log(self, B3MOD_LOG_INFO, "hello");
    return 0;   /* 0: loaded */
}
```

## Building

The library's name is the mod folder's name with `-` as `_` (`my-mod` -> `my_mod`), in `bin/<system>/`.

```sh
make -C sdk MOD=path/to/my-mod                     # Linux or macOS (this system)
make -C sdk MOD=path/to/my-mod SYSTEM=windows      # Windows, from Linux with MinGW-w64 (x86_64-w64-mingw32-gcc)
make -C sdk MOD=path/to/my-mod clean
```

```bat
rem Windows, MinGW-w64's bin folder on PATH
sdk\build_windows.cmd path\to\my-mod
```

```sh
cmake -S sdk -B build/my-mod -DMOD=path/to/my-mod     # any system with CMake
cmake --build build/my-mod
```

A mod that ships for every system has all three libraries in `bin/`; the game loads the one for its system. The flags
the templates use matter: `-fvisibility=hidden` (only `b3mod_main` and `b3mod_built_for` are exported),
`-fexceptions` (the game may unwind through a hook when a run ends inside a game call), and on Windows
`-static-libgcc` (no MinGW DLL beside the mod). On macOS the library is ad-hoc signed.

## Trying it

Put the mod folder in the mods folder (the launcher: Mods > Open mods folder), press Look again and turn it on. A code
mod runs as a program on the player's PC, so the launcher asks once per library before it may run. The game's log
says what the mod did: `native_play: mods: my-mod: code loaded (...)`, or why it was not loaded.

For a test without the launcher (offscreen, as the port's own checks run):

```sh
build/native_play --game <disc> --offscreen --mods my-mod --mods-dir path/to/mods-folder --render-args "--offscreen-live out --shots 9000"
```
