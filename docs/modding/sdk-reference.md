# Code-mod SDK reference

The C API a code mod uses, as `sdk/include/b3mod_api.h` defines it (ABI version 1, mod API 2). The plan behind it:
[DESIGN.md](DESIGN.md) §2. How hooks reach the game's functions: [hooks.md](hooks.md). Building: [sdk/README.md](../../sdk/README.md).
Tutorials: [03 a settings row](tutorials/03-a-settings-row.md), [05 an overlay](tutorials/05-an-overlay.md). Script mods
(Lua, no compiler) run on this same API inside the game: [lua-reference.md](lua-reference.md).

## What a code mod is

A shared library (`.dll`, `.so`, `.dylib`) named by the `[code]` section of a mod's `mod.ini`. The game (native_play)
runs the game as native C++ compiled from the decompilation; it does not run the disc's program. So a code mod changes
the game the way the port's own features do: it hooks the game's functions **by their retail address** (the EE address
in SLUS-21050) and runs its own C code there.

```ini
[mod]
id = my-mod
version = 1.0
api = 2                 # code mods need api 2

[code]
windows = bin/windows/my_mod.dll
linux = bin/linux/my_mod.so
macos = bin/macos/my_mod.dylib
gameplay = no           # yes: it changes physics, AI or scoring
platform = no           # yes: it hooks the platform tier (works only with "Developer mods" on)
```

| Key | Meaning |
|---|---|
| `windows`, `linux`, `macos` | the library for that system, a path inside the mod (no `..`). At least one. The game loads its own system's. |
| `gameplay` | the author's statement: `yes` when the mod changes physics, AI or scoring. Shown on the Mods page. |
| `platform` | `yes` lets the mod hook the platform tier (the kernel, the SIF / disc / pad drivers: `src/native/platform.h`), and only while the user's "Developer mods" is on. |

A manifest with `[code]` and `api = 1` cannot be used; a mod without `[code]` may say `api = 1` or `api = 2`.

## Loading

native_play loads the active code mods **in load order** after the port's own hooks are on the hook table and before
the game's first instruction:

1. the library is opened (`LoadLibraryExW` with its absolute path; `dlopen(RTLD_NOW | RTLD_LOCAL)`);
2. `b3mod_built_for()` must return a version from 1 up to the port's `B3MOD_API_VERSION` (else: "a newer port is
   needed");
3. `b3mod_main(api, self)` registers the mod's hooks, settings rows, callbacks and arena memory and returns 0;
4. its hooks go on the hook table as `mod:<id>`, all or nothing.

If any step fails, the mod is **not loaded at all**: its arena memory is given back, its rows, callbacks and writes are
dropped, its library is closed, and its files are left out of the run too (the set is printed again, the renderer is
told). The log says why:

```
native_play: mods: clash: not loaded: clash conflicts with good: both replace 0x00100000 leaf_a
```

Refusals: no library for this system; the library does not load; no `b3mod_main` or no `b3mod_built_for`;
`b3mod_main` returns non-zero; a registration call refused inside `b3mod_main` (a hook on an address without a native
function, a hook on the platform tier without `platform = yes` and Developer mods, a second replacement of an address
already replaced by a mod higher in the load order, a bad settings row, an arena request that does not fit); and, in a
window run, a library the user has not allowed (below).

The Mods page shows each code mod's result at the last start (`<user data>/mods-state.ini`).

**Trust.** A code mod is a program: nothing stops a library from calling the operating system. The API's limits are
there for correctness and determinism, not security. The launcher asks the user once per mod and set of files when the
mod is turned on or the game started: the permission, kept in `<user data>/mods-trusted.ini`, is for the mod's id and
one SHA-256 over every library file in the mod's folder (any `.dll`, `.so`, `.so.<n>` or `.dylib`, in any subfolder,
and the library `[code]` names), so a new version that changes the main library or only a library it loads is asked
about again. A window run refuses a mod whose id and set were not allowed, and the game hashes the set again right
before it loads the library (on Windows the files are held against writes from that hash to the load), so a file
changed after the check is refused too. On Linux and macOS a short window stays between that last hash and `dlopen`
(loading through `/proc/self/fd` would break the library's `$ORIGIN` lookup of the libraries beside it); only a
program already able to write the mod's folder could use it. Offscreen runs (`--offscreen`: the port's own checks and test scripts naming
`--mods`) are not asked: that is test-only behaviour, the launcher never starts the game offscreen. Install code mods
only from people you trust, preferably with their source.

**The mod hash** covers every library `[code]` names (`code <system> <path> <size> <fnv>` lines in the mod hash,
[mod-format.md](mod-format.md)), so the set hash in logs, pad recordings and replays changes when a library does.

## Entry points

```c
B3MOD_DECLARE_VERSION()                                               /* once, in one source file */
B3MOD_EXPORT int b3mod_main(const B3ModApi* api, B3ModSelf* self);    /* 0: loaded */
```

`B3MOD_DECLARE_VERSION()` defines `unsigned b3mod_built_for(void)`, returning the `B3MOD_API_VERSION` of the header the
mod was built with. Keep `api` and `self` for later calls; both stay valid for the whole run.

## Rules

- **Versioning.** `B3ModApi` starts with `size` and `version`. New members are only added at the end; a mod built with
  a newer header checks `api->size` before using a member the running port may not have. A change to an existing
  member is a new `B3MOD_API_VERSION`. The port supports its current version and the one before.
- **C only.** No C++ type crosses the ABI; every string is UTF-8; every guest address is a 32-bit EE address.
- **Threads.** Every API function and every callback runs on the game's thread. A mod needs no locks for its own state.
- **Registration** (`replace`, `wrap`, `alloc`, `add_setting`, `on_vblank`, `on_frame`, `on_shutdown`) works only inside
  `b3mod_main`; afterwards it is refused and logged once. The hook table is fixed for the run.
- **Determinism.** Hooks run in call order on virtual time. A mod that bases what the game does on `vblank()`, guest
  memory and its settings (never the wall clock, never host randomness) replays exactly from a pad recording or a
  `.b3replay` made with the same mod set.

## Types

| Type | What |
|---|---|
| `B3ModSelf` | the loaded mod (opaque); passed to every call made for it |
| `B3ModHookCtx` | one running hook (opaque, valid during its call only); `next` runs the layer below |
| `B3ModRegs` | a call's argument registers: `gpr[8]` = a0, a1, a2, a3, t0, t1, t2, t3 (low 64 bits; 32-bit values sign-extended), `gpr_hi[8]` their upper halves (128-bit arguments), `fpr[8]` = f12..f19 raw bits, `sp` the guest stack pointer (stack arguments from `sp + 0`) |
| `B3ModResult` | a call's results: `v0` (with `v0_hi`, 128 bits), `v1`, `f0` raw bits |
| `B3ModHookFn` | `void fn(B3ModHookCtx* ctx, const B3ModRegs* regs, B3ModResult* result, void* user)` |
| `B3ModEventFn` | `void fn(void* user)` |
| `B3ModSetting` | a settings row: `size` (= `sizeof(B3ModSetting)`), `kind`, `key`, `label`, `description`, `values`, `count`, `default_value` |
| `B3MOD_RGBA(r, g, b, a)` | a colour, red in the low byte; alpha 0 transparent .. 255 opaque |

## The API

### Info

| Member | Does |
|---|---|
| `const char* port_version(void)` | the port's version line |
| `uint64_t vblank(void)` | the virtual VBlank count (60 per second of game time): the clock to use |
| `const char* mod_id(const B3ModSelf*)` | the mod's id |
| `const char* mod_dir(const B3ModSelf*)` | the mod's folder, absolute |
| `void log(B3ModSelf*, int level, const char* text)` | one line in the game's log, `mod <id>: <text>` (`B3MOD_LOG_INFO`, `_WARN` adds "warning: ", `_ERROR` "error: ") |

### Hooks

| Member | Does |
|---|---|
| `int replace(self, addr, fn, user)` | `fn` runs instead of the function at `addr`. One mod per address: a second mod replacing it is a conflict, and that mod is not loaded. The port's own wrappers stay above it. 1 registered, 0 refused. |
| `int wrap(self, addr, fn, user)` | `fn` runs around the function at `addr`: any number of mods per address, the first in the load order on top. |
| `void next(ctx, regs, result)` | inside a hook: run the layer below (the game's function, a replacement, the port's wrappers) with these registers |
| `int call(self, addr, regs, result)` | call the game's function at `addr` through every hook on it (`regs->sp` 0: the current guest stack). 0: no native function there, nothing called. |
| `int has_function(self, addr)` | 1 when the function at `addr` is native (hookable and callable) |

The layers at an address, bottom to top: the game's function, a mod's `replace`, the port's wrappers, the mods' `wrap`s
([hooks.md](hooks.md)). A hook sees every call that goes through dispatch; a direct C++ call between two generated
units does not ([hooks.md](hooks.md) "What a hook sees", `config/mod_hooks.txt`). Addresses: the decomp's
`config/symbol_addrs.txt`, `gen/native/manifest.tsv` (every native function, its address and name).

A hook that does not call `next` replaces the call. The result a hook leaves in `result` is what the caller gets:

```c
static void lookup(B3ModHookCtx* ctx, const B3ModRegs* regs, B3ModResult* result, void* user) {
    if ((uint32_t)regs->gpr[1] == 230) { result->v0 = my_text; return; }   /* answer it ourselves */
    api->next(ctx, regs, result);                                          /* else the game's */
}
```

### Guest memory

| Member | Does |
|---|---|
| `int read(self, addr, dst, n)` | copy `n` bytes from guest memory (EE RAM, 32 MB; the scratchpad 0x70000000). 0 when a byte is outside them. |
| `int write(self, addr, src, n)` | copy `n` bytes into guest memory. 0 outside RAM and the scratchpad, and inside the program's code (0x00100000-0x00483F00): that code never runs natively; change a function with a hook. Writes made inside `b3mod_main` are applied once the mod has loaded. |
| `uint32_t alloc(self, bytes, align)` | inside `b3mod_main`: `bytes` of zeroed guest memory of the mod's own, at a multiple of `align` (a power of two up to 4096), from the mod arena (0x000C8000 up to the program, 0x00100000; [hooks.md](hooks.md) "The mod arena"). The same address in every run with the same mods. 0: none left (and the mod is not loaded). |

### Settings

| Member | Does |
|---|---|
| `int add_setting(self, const B3ModSetting*)` | inside `b3mod_main`: a row on PC SETTINGS > MODS (in the game) and on the launcher's Mods page. 1 added, 0 refused (the mod is then not loaded). |
| `int get_setting(self, key)` | the row's value now (an index), -1 for a key the mod did not add |

`B3ModSetting.kind`: `B3MOD_SETTING_TOGGLE` (values 0 Off, 1 On; `values` NULL, `count` 0 or 2),
`B3MOD_SETTING_CHOICE` (`count` names in `values`, 2 to 16, each 1 to 24 characters), `B3MOD_SETTING_SLIDER`
(`count` levels, 2 to 101; the value is the level). `key`: a-z, 0-9, `_`, `-`, `.`, 1 to 32 characters, unique in the
mod. `label`: up to 40 characters (the game's page shows it in upper case). `default_value`: an index below `count`.

Where the values live and when they change:

- a window run reads `<user data>/mod-settings/<id>.ini` (`[settings]`, then `<key> = <value>`: Off / On, a choice's
  name, a slider's level); a value that is not one of the row's is replaced by the default, with a log line. Offscreen
  runs take the defaults (`--mod-set` below sets others);
- a change made on the page while the game runs reaches the mod **at the next VBlank boundary**, before the VBlank's
  `on_vblank` callbacks, and is saved to the file; the launcher's Mods page changes the file (used from the next start);
- every value is recorded: a pad recording gets `set 0 <mod>.<key> <value>` for each row at the start and `set
  <vblank> <mod>.<key> <value>` for each change; a `.b3replay` the setting `mod.<mod>.<key>`. A replay applies them on
  the same VBlanks and ignores the page;
- `native_play --mod-set "[<vblank>:]<mod>.<key>=<value>,..."` (or `B3NATIVE_MOD_SET`) sets values from the start, or
  at a VBlank, as a script; recorded the same way.

### Overlays

| Member | Does |
|---|---|
| `uint32_t texture(self, w, h, rgba)` | a texture from `w` x `h` straight-alpha RGBA pixels, rows top first, up to 1024 x 1024; its id (0: refused). A mod may have 64 textures, 16 MB of pixels in all (one log line, then 0); a mod that does not load has its textures dropped |
| `void quad(self, x, y, w, h, rgba, texture)` | a rectangle filled with `rgba`, or the texture tinted by it (`texture` 0: none) |
| `void text(self, x, y, size, rgba, utf8)` | text from its top left corner, letters `size` high, in the port's stroke font: A-Z, 0-9, space and `+ - / . : ;` (lower case is drawn as upper case) |

Coordinates are the game's 640 x 448 screen (the 4:3 picture; top left 0, 0), placed in the window as the PC SETTINGS
page is. What a mod draws between two game frames is shown from the next frame until the frame after, drawn by the
renderer after the game's HUD and under the PC SETTINGS page (not in photo mode). Draw it again in every `on_frame`. Up
to 256 items per mod and frame; the first mod in the load order is drawn on top. Drawing never touches guest memory: an
overlay changes no replay.

### Events

| Member | Does |
|---|---|
| `int on_vblank(self, fn, user)` | inside `b3mod_main`: `fn(user)` at every virtual VBlank, after the port's VBlank work (this VBlank's settings changes are in) |
| `int on_frame(self, fn, user)` | inside `b3mod_main`: at every game frame (the game's pad poll, or the frame's end when it reads no pad), before the frame goes to the renderer: draw overlays here |
| `int on_shutdown(self, fn, user)` | inside `b3mod_main`: when the run ends normally (not after a crash), in the reverse of the load order |

Callbacks of several mods run in load order.

### Files (read only)

| Member | Does |
|---|---|
| `int64_t data_size(self, path)` | a file's size, -1 when there is none |
| `int64_t read_data(self, path, offset, dst, n)` | up to `n` bytes from `offset`; the bytes read, or -1 |

`path` is either `disc:<disc path>` (`disc:DATA/GLOBALUS.BIN`: a disc file as the game gets it, through the active
mods' files) or a path in the mod's own folder (`data/table.bin`; no `..`, no absolute path). No other file access is
offered (a library can of course call the operating system: see Trust).

## Checks

`tests/test_code_mods.cpp` (in `make test`) checks the struct layout and version, the loader's refusals, replace /
replace conflicts (the loser not loaded, nothing of it left), the API on a synthetic hook table, and the settings'
recording and replay. The examples (`examples/mods/hello-overlay`, `examples/mods/menu-label-setting`) are checked in
the game with `tools/native/race_check.sh` and `MODS` / `MODS_DIR` (the tutorials show the runs).
