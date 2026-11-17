# Tutorial 05: an overlay

In this tutorial you make a code mod that **draws over the game**: a small box in the top left corner with the game's
VBlank counter and a little picture of your own. Overlays are drawn by the renderer after the game's HUD; they never
change the game, so a replay plays the same with or without them. About fifteen minutes; you need a C compiler and the
port's `sdk` folder. Tutorial [03](03-a-settings-row.md) explains the parts every code mod has (`mod.ini`,
`b3mod_main`, building, allowing the code); this one adds drawing.

The finished mod is [examples/mods/hello-overlay](../../../examples/mods/hello-overlay).

## 1. The mod

`mod.ini` names the library on each system, as in tutorial 03:

```ini
[mod]
id = hello-overlay
name = Hello overlay
version = 1.0
api = 2

[code]
windows = bin/windows/hello_overlay.dll
linux = bin/linux/hello_overlay.so
macos = bin/macos/hello_overlay.dylib
gameplay = no
```

`src/hello_overlay.c`:

```c
#include "b3mod_api.h"
#include <stdio.h>

static const B3ModApi* api;
static B3ModSelf* self;
static uint32_t badge;   /* a texture: an 8 x 8 orange and white checker board */

static void draw(void* user) {
    char text[40];
    snprintf(text, sizeof text, "VBLANK %llu", (unsigned long long)api->vblank());
    api->quad(self, 20, 20, 190, 28, B3MOD_RGBA(0, 0, 0, 170), 0);           /* the box */
    api->quad(self, 20, 20, 5, 28, B3MOD_RGBA(255, 150, 0, 255), 0);         /* an orange edge */
    api->text(self, 33, 28, 12, B3MOD_RGBA(255, 255, 255, 255), text);       /* the counter, 12 units high */
    if (badge) api->quad(self, 188, 26, 16, 16, B3MOD_RGBA(255, 255, 255, 255), badge);   /* the texture, as it is */
}

B3MOD_DECLARE_VERSION()

B3MOD_EXPORT int b3mod_main(const B3ModApi* a, B3ModSelf* s) {
    uint8_t px[8 * 8 * 4];
    ...                                                                      /* fill the 8 x 8 RGBA pixels */
    api = a;
    self = s;
    badge = api->texture(self, 8, 8, px);
    if (!api->on_frame(self, draw, NULL)) return 1;
    api->log(self, B3MOD_LOG_INFO, "drawing the VBlank counter in the top left corner");
    return 0;
}
```

How it works:

- `on_frame` registers `draw`, which the game calls at **every game frame** (each time the game reads the pad, about
  60 times a second in a race, 30 in the menus). What a mod draws during a frame is shown from the next frame until the
  frame after, so `draw` draws the whole overlay every time.
- Coordinates are the game's **640 x 448 screen**, top left 0, 0, whatever the window's size: the renderer places that
  screen in the window as it does the PC SETTINGS page.
- `quad` fills a rectangle with a colour (`B3MOD_RGBA(red, green, blue, alpha)`; alpha 170 lets the game show through),
  or shows a texture, tinted by the colour (white: as it is).
- `text` writes with the port's stroke font: capital letters, digits, space and `+ - / . : ;`. Its size is the height
  of a capital letter.
- `texture` turns RGBA pixels (red, green, blue, alpha per pixel, rows from the top) into a texture once, in
  `b3mod_main`; its id goes into `quad`.
- `api->vblank()` is the game's own clock: 60 VBlanks per second of game time. It stops when the game pauses for the
  window, and a replay has the same values.

## 2. Build it

```
$ make -C sdk MOD=examples/mods/hello-overlay
b3mod: built .../examples/mods/hello-overlay/bin/linux/hello_overlay.so
```

Windows with MinGW-w64: `sdk\build_windows.cmd examples\mods\hello-overlay`; macOS: the same `make` line; anywhere
with CMake: `cmake -S sdk -B build/hello -DMOD=examples/mods/hello-overlay && cmake --build build/hello`.

## 3. See it

Copy the `hello-overlay` folder into the mods folder, press Look again, turn it on (and allow its code, as in tutorial
03) and start the game. The log says:

```
mod hello-overlay: drawing the VBlank counter in the top left corner
native_play: mods: hello-overlay: code loaded (0 replaced, 0 wrapped, 0 settings, 1 callbacks)
```

From the first frame on, the top left corner shows the box: `VBLANK 17788` on the main menu, `VBLANK 20808` in the race,
with the orange and white square at its right end. It is drawn over the HUD and the menus, under the PC SETTINGS page,
and not in photo mode.

Without a window (the port's checks), take screenshots and look at them:

```
$ MODS=hello-overlay MODS_DIR=examples/mods SHOTS=8850,9000 STRICT=1 tools/native/race_check.sh build/native_play build/rc_overlay
...
race_check: PASS race US/C2_V2 reached and driven to VBlank 23400; ...
$ grep "mod overlay" build/rc_overlay/native_play.log
[b3live] shot poll 8850: mod overlay drawn (4 of 4 items, record 8841)
[b3live] shot poll 9000: mod overlay drawn (4 of 4 items, record 8991)
[b3live] shot poll 10700: mod overlay drawn (4 of 4 items, record 10688)
[b3live] shot poll 11500: mod overlay drawn (4 of 4 items, record 11488)
```

The shots are `build/rc_overlay/shots/live_<poll>_0.png`. Because the overlay is drawn only, the run's game is the
same as without the mod; its replay (`tools/native/replay_check.sh` with the same `MODS` and `MODS_DIR`) plays back
equal.

## 4. Make it yours

- **Move it**: the screen is 640 x 448, so a box at
  `x = 230, y = 400` sits at the bottom middle (look at a race shot for where the game's HUD is).
- **Show game state**: `api->read(self, address, &value, 4)` reads the game's memory (the decomp and
  `gen/native/manifest.tsv` name the data); draw what you read. Reading never changes the game.
- **More than one picture**: make each texture once in `b3mod_main` and keep the ids; `quad` with a part of the
  screen per picture. Textures are up to 1024 x 1024.
- **Turn it off and on**: add a settings row (tutorial 03) and draw only while it is On.

Up to 256 items per frame. The full list of calls: [the SDK reference](../sdk-reference.md#overlays).
