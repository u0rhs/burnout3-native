# Hooks on game functions

How the port and code mods change what a game function does. The design is in [DESIGN.md](DESIGN.md) §2.3 and §2.4;
the C API mods use is in [sdk-reference.md](sdk-reference.md) (`replace`, `wrap`, `next`, `call`). Every function the
port runs natively, by retail address and name, is in `gen/native/manifest.tsv`; the decomp names the rest. (A list
of which addresses every caller reaches through dispatch is not generated yet: see "What a hook sees".)

## The native table and the hook table

native_play runs the game as C++ generated from the decomp. Every game function that is native has a **row** in the
native table (`src/native/native_table.cpp`), keyed by its retail guest address (the EE address in SLUS-21050, as in
`config/symbol_addrs.txt` and `gen/native/manifest.tsv`). When the game calls a function by address, dispatch
(`b3host::dispatch`, `include/b3host/call.h`) looks the address up and runs the row's entry.

The table decides the row at an address when it is built: the platform tier's rows (`src/native/platform.h`:
`B3NATIVE_FIRST`, the movie libraries, the kernel / SIF / drive functions) take the place of a generated function at
the same address. For the hook table, that row is the bottom layer.

The **hook table** (`src/native/hook_table.h`) is the one place that changes what a row runs. Per address the layers
are fixed, bottom to top:

| Layer | What | Order inside the layer |
|---|---|---|
| 0 | the row: the generated function (or the libm / platform row) | -- |
| 1 `kReplace` | a code mod's `replace` | at most one per address (a second mod replacing it is not loaded) |
| 2 `kPort` | the port's own wrappers | install order: the last installed is on top |
| 3 `kWrap` | the code mods' `wrap`s | load order: the first mod in the list is on top |

Dispatch runs the top layer. Each hook gets a `next` entry, the layer below it, and decides whether to call it.
So a port feature keeps working over a mod's replacement, and a mod's wrap sees the game as the player does. An
address with no hooks runs its row's entry directly, as before the table existed.

A hook is a plain entry `void (const b3host::CallArgs&, b3host::CallResult&)`: the EE argument registers (a0-a3,
t0-t3, f12-f19, sp) in, v0 / v1 / f0 out. A hook that needs its own data is bound with `bind_hook(address, layer, fn,
user)`, where `fn` gets `(args, result, user, next)`. This is the form the code-mod API wraps (`src/native/code_mods.cpp`: a C trampoline per hook).

### Installing

A set of hooks goes in under one owner name (`port:pc_menu`, later `mod:<id>`), all or nothing. The table refuses a
set, and changes nothing, if:

- an address has no row (the function is not native, or the decomp inlined it);
- a mod layer hooks a platform-tier row (allowed only with `[code] platform = yes` and the user's "Developer mods");
- a second replacement targets an address: `mod-b conflicts with mod-a: both replace 0x003FBC20 func_003FBC20`;
- the owner already installed a set, or the table is sealed.

native_play seals the table after the port's features and the code mods (as `mod:<id>`, in load order) are installed, before the game
starts. Nothing is added during a run.

### The port's own hooks

Installed by `tools/native/native_play.cpp` in this order (all on the port layer):

| Owner | Addresses | When | File |
|---|---|---|---|
| `port:explore_retail` | the listed ones (switched off: the fallback runs retail code) | `--explore-retail` only | `tools/native/explore_standins.h` |
| `port:pc_menu` | 0x321470, 0x31C350, 0x3181E0, 0x342090 | unless `B3NATIVE_PC_ROW=0` | `tools/native/pc_menu.h` |
| `port:card_silence` | 0x449F10, 0x449150 | with the port's save folder, unless `B3NATIVE_CARD_MESSAGES=1` | `tools/native/card_silence.h` |
| `port:date_format` | 0x271A00 | always | `tools/native/date_format.h` |
| `port:online_message` | 0x271A00 | unless `B3NATIVE_ONLINE_MESSAGE=0` | `tools/native/online_message.h` |
| `port:speed_units` | 0x1A3C70, 0x1A3B30, 0x1A2700, 0x3CAAB0, 0x3D8920, 0x364300, 0x3B8670, 0x376880, 0x376F60, 0x43A420, 0x3F1490 | always (with MPH, the default, it only calls the next layer) | `tools/native/speed_units.h` |
| `port:custom_soundtrack` | 0x3FBC20, 0x3865A0, 0x386290, 0x386240, 0x3863C0, 0x385F20, 0x386410, 0x386080, 0x386020, 0x438F10 | unless `B3NATIVE_MUSIC_OFF=1` | `tools/native/custom_soundtrack.h` |
| `port:debug_health`, `port:call_count` | 0x1A2700; the given one | test and debugging variables only | `tools/native/debug_health.h` |
| `port:time_trace` | the listed ones | `B3NATIVE_TIME_TRACE` only | `tools/native/time_trace.h` |
| `port:menu_mouse` | 0x31FF80, 0x351410 | unless `B3NATIVE_MENU_MOUSE=0` | `tools/native/menu_mouse.h` |
| `port:diff_check`, `port:poison_check` | the listed ones | `B3NATIVE_DIFF`, `B3NATIVE_POISON` only | `tools/native/diff_check.h`, `poison_check.h` |

## What a hook sees

A hook runs for every call that goes through dispatch: calls from code that is not native, virtual calls (`vcall`),
function pointers (`gfn`), `call_guest`, and every call to an `extern "C"` decomp function (the generator routes
C-linkage callees through `guest_fn`).

A **direct C++ call between two generated units bypasses dispatch**: when a unit calls a C++ function that another
selected unit defines (a member of a class from a shared decomp header), the generator links the call directly, and a
hook on that function does not see it.

`config/mod_hooks.txt` closes this for chosen functions. For each listed address the generator (decomp2host) puts two
lines at the top of the function's definition (`include/b3host/hook_route.h`):

```cpp
b3host::RouteScope b3_route_scope;
if (b3host::route_call<0xADDR>(b3_route_scope)) return b3host::call_guest<R>(0xADDR, /* this, the arguments */);
```

With no hook on the function this is one load and a branch, and the body runs. With a hook, a call that arrived by a
direct link (or a direct recursive call from the body) is sent through dispatch, so the hook layers run, and the
bottom layer comes back into the body. A call that came through dispatch runs the body. Restrictions: the definition
needs a return type (no constructor or destructor) and no struct parameter by value. After editing the list,
regenerate (`make native-bundle`) and commit `gen/native`. The list is empty today.

## The mod arena

Guest RAM from 0xC0000 to the ELF's first byte (0x100000) is free: the native game never uses it. One allocator,
`b3native::GuestArena` (`src/native/hook_table.h`), hands it out:

| Range | Use |
|---|---|
| 0xC0000-0xC0400 | `port:pc_menu`: the main menu's 7-row copies, help texts, row labels |
| 0xC0800-0xC0820 | `port:date_format`: the day-first date string |
| 0xC0C00-0xC0F00 | `port:custom_soundtrack`: the now-playing texts |
| 0xC0F00-0xC1200 | `port:now_playing`: another music app's popup texts (OTHER MUSIC APPS) |
| 0xC1200-0xC1300 | `port:online_message`: the ONLINE dialog's text |
| up to 0xC8000 | reserved for the port's own regions |
| 0xC8000-0x100000 | code mods (`alloc` in `b3mod_main`), in load order and call order; a mod that does not load gives its memory back |

The port's regions keep the addresses they had before the arena existed. Mod allocations are deterministic: the same
set of mods, making the same calls, gets the same addresses in every run. Each user checks that its region reads zero
before its first write and stays off if it does not.

## Checks

`tests/test_hook_table.cpp` (in `make test`) covers the layer order, next-chaining, all-or-nothing installs,
replace / replace conflicts, sealing, switch-offs, bound hooks, dispatch through the published table, the route
prologue, and the arena's determinism and bounds. With no mods, the golden compare must pass unchanged.
