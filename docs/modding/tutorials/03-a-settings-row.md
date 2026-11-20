# Tutorial 03: a settings row

In this tutorial you make a **code mod**: a small C library the game loads before it starts. It adds a row to the
game's PC SETTINGS page (a new MODS tab) and, while the row is On, the main menu's **SINGLE EVENT** reads
**QUICK RACE**. Tutorial 02 changed that text with a data mod (a new string table); here the text stays the game's, and
a hook on the game's string lookup answers that one string with your own text when the player wants it. About twenty
minutes; you need a C compiler and the port's `sdk` folder.

What you will do:

1. look at the mod's two files;
2. find the game function to hook and the string's number;
3. build the library;
4. install the mod, allow its code, and use the row in the game;
5. check it without a window (optional).

The finished mod is [examples/mods/menu-label-setting](../../../examples/mods/menu-label-setting). Read
[sdk/README.md](../../../sdk/README.md) for the build tools on each system, and keep the
[SDK reference](../sdk-reference.md) at hand for the details of each call.

## 1. The mod's files

```
menu-label-setting/
    mod.ini
    src/menu_label_setting.c
```

`mod.ini` says the mod is a code mod (`api = 2`) and where its library is on each system:

```ini
[mod]
id = menu-label-setting
name = Menu label setting
version = 1.0
description = A settings row (Off / On) that makes the main menu's SINGLE EVENT read QUICK RACE (a code-mod example).
api = 2

[code]
windows = bin/windows/menu_label_setting.dll
linux = bin/linux/menu_label_setting.so
macos = bin/macos/menu_label_setting.dylib
gameplay = no
```

`gameplay = no` is your statement that the mod does not change physics, AI or scoring. The source has one function the
game calls, `b3mod_main`, and one hook:

```c
#include "b3mod_api.h"

#define STRING_LOOKUP 0x00271A00u   /* func_00271A00 (a0: the string table, a1: the string id) -> v0: UTF-16 text */
#define SINGLE_EVENT 230u           /* GLOBALUS.BIN string 230 "SINGLE EVENT" */

static const B3ModApi* api;
static B3ModSelf* self;
static uint32_t text_addr;          /* our "QUICK RACE" in guest memory (UTF-16) */

static void lookup(B3ModHookCtx* ctx, const B3ModRegs* regs, B3ModResult* result, void* user) {
    if ((uint32_t)regs->gpr[1] == SINGLE_EVENT && api->get_setting(self, "quick_race") == 1) {
        result->v0 = text_addr;     /* the game draws our text instead */
        return;
    }
    api->next(ctx, regs, result);   /* everything else: the game's own lookup */
}

B3MOD_DECLARE_VERSION()

B3MOD_EXPORT int b3mod_main(const B3ModApi* a, B3ModSelf* s) {
    static const char text[] = "QUICK RACE";
    uint16_t utf16[sizeof text];
    B3ModSetting row = {sizeof(B3ModSetting), B3MOD_SETTING_TOGGLE, "quick_race", "Quick race label",
                        "SINGLE EVENT on the main menu reads QUICK RACE.", 0, 0, 0};
    api = a;
    self = s;
    if (!api->add_setting(self, &row)) return 1;
    text_addr = api->alloc(self, sizeof utf16, 16);
    ...                             /* "QUICK RACE" as UTF-16 into utf16 */
    if (!api->write(self, text_addr, utf16, sizeof utf16)) return 3;
    if (!api->wrap(self, STRING_LOOKUP, lookup, 0)) return 4;
    return 0;
}
```

What each part does:

- `B3MOD_DECLARE_VERSION()` tells the game which version of the API the mod was built with.
- `add_setting` adds the row: a toggle (Off / On), key `quick_race`, Off by default. The game's page shows it under
  PC SETTINGS > MODS, the launcher on the mod's card. `get_setting(self, "quick_race")` reads it: 0 Off, 1 On.
- `alloc` gives the mod 22 bytes of the game's memory of its own (the same address in every run), and `write` puts the
  text there. The game's code can only use text that is in its own memory, so the hook must answer with an address
  there, not with a pointer into the mod.
- `wrap` puts `lookup` in front of the game's string lookup. For every call, `lookup` gets the call's registers: `gpr[0]`
  is the first argument (the string table), `gpr[1]` the second (the string's number). For string 230 with the row On
  it sets the result itself; every other call goes on to the game's lookup with `api->next`.
- Returning 0 from `b3mod_main` means "loaded". Any registration that fails (here: a non-zero return, a refused call)
  leaves the whole mod out, and the game's log says why.

## 2. Finding the function and the string

The **string's number** comes from the text the game shows, as in tutorial 02:

```
$ b3modtool strings export <disc>/DATA/GLOBALUS.BIN work/strings.txt
$ grep -n "= SINGLE EVENT" work/strings.txt
...: 230 = SINGLE EVENT
```

The **function**: every text on screen comes through the string lookup at retail address 0x00271A00 (the port's own
date-format setting hooks the same function, `tools/native/date_format.h`). Hooks use the address the function has in
the retail program, which never changes; `gen/native/manifest.tsv` lists every function the port runs natively, by
address:

```
$ grep -i "^00271A00" gen/native/manifest.tsv
00271A00	matched	func_00271A00	src/b3/_unclassified/func_00271A00.cpp	8	0c9bb020afcfbbfa	namespace b3gen_func_00271A00; calls 1
```

A function that is not in that list cannot be hooked (`api->has_function` says the same at run time). How hooks see a
call, and the one case they do not: [hooks.md](../hooks.md).

## 3. Build the library

From the port's folder (the `sdk` folder is all the build needs):

```
$ make -C sdk MOD=examples/mods/menu-label-setting
b3mod: built .../examples/mods/menu-label-setting/bin/linux/menu_label_setting.so
```

On Windows with MinGW-w64 (its `bin` folder on PATH):

```
> sdk\build_windows.cmd examples\mods\menu-label-setting
b3mod: built ...\examples\mods\menu-label-setting\bin\windows\menu_label_setting.dll
```

On macOS the same `make` line builds `bin/macos/menu_label_setting.dylib`. CMake works everywhere too
(`cmake -S sdk -B build/menu -DMOD=examples/mods/menu-label-setting && cmake --build build/menu`). Each build puts the
library where `mod.ini` says.

## 4. Install it and use the row

1. Copy the `menu-label-setting` folder into the mods folder (launcher: Mods > Open mods folder) and press **Look
   again**. Its card says "Contains code: runs as a program on your PC".
2. Press **Turn on**. Because the mod contains code, the launcher asks once whether you allow it to run (a code mod is
   a program; allow only code from people you trust). Choose **Allow and turn on**. A change to any library in the mod
   (a new version) is asked about again.
3. Start the game. Its log has:

   ```
   native_play: mods: menu-label-setting: contains code (.../bin/linux/menu_label_setting.so)
   native_play: mods: menu-label-setting: code loaded (0 replaced, 1 wrapped, 1 settings, 0 callbacks)
   ```

4. Open PC SETTINGS (the main menu's SETTINGS row), press R1 until the **MODS** tab, and set **QUICK RACE LABEL** to
   On. The change reaches the mod at the next VBlank. The main menu reads its labels when it is built, so the new
   label shows the next time the game builds the main menu (at the next start, the value is saved in
   `mod-settings/menu-label-setting.ini` in the user data folder). The launcher's Mods page shows the row too, under
   the mod's card, after the game has run once with the mod.

If the mod does not load, its card and the log say why: for example `not loaded: wrap 0x00271A01: no native function
there` for a mistyped address.

## 5. Check it without a window (optional)

The port's own checks run the game offscreen and take screenshots. A settings value can be set from the start or at a
VBlank with `--mod-set` (here through `B3NATIVE_MOD_SET`, which race_check passes on):

```
$ B3NATIVE_MOD_SET="16000:menu-label-setting.quick_race=On" MODS=menu-label-setting MODS_DIR=examples/mods \
  SHOTS=8850,9000 STRICT=1 tools/native/race_check.sh build/native_play build/rc_label
...
race_check: PASS race US/C2_V2 reached and driven to VBlank 23400; ...
$ grep "quick_race" build/rc_label/native_play.log build/rc_label/pads.txt
build/rc_label/native_play.log:native_play: VBlank 16000: mods: setting menu-label-setting.quick_race = On
build/rc_label/pads.txt:set 0 menu-label-setting.quick_race Off
build/rc_label/pads.txt:set 16000 menu-label-setting.quick_race On
```

`build/rc_label/shots/live_8850_0.png` shows the main menu with QUICK RACE. The pad recording (`pads.txt`) holds the
row's value at the start and its change on VBlank 16000, so replaying it (`native_play --replay-pad`) changes the row on
the same VBlank; a `.b3replay` keeps it the same way (`tools/native/replay_check.sh` with the same `MODS`, `MODS_DIR` and
`B3NATIVE_MOD_SET` runs the scenario recorded and played back, and compares the two). `tools/modding/sdk_check.sh` does
this tutorial's build and runs in one go.

## Where next

- [Tutorial 05](05-an-overlay.md): drawing over the game.
- [The SDK reference](../sdk-reference.md): every call, the rules for hooks, determinism and versions.
- Choices and sliders: `B3MOD_SETTING_CHOICE` with a list of names, `B3MOD_SETTING_SLIDER` with a number of levels; the
  value is always an index.
