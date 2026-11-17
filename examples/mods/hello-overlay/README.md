# hello-overlay: draw over the game

A code mod (the mod kit's SDK): every game frame it draws a small box in the top left corner with the game's VBlank
counter and an 8 x 8 checker-board texture. It only draws, so the game and its replays are the same with it.
[Tutorial 05](../../../docs/modding/tutorials/05-an-overlay.md) explains each line.

Build (the library lands in `bin/<system>/`, where `mod.ini` names it):

```sh
make -C sdk MOD=examples/mods/hello-overlay              # Linux, macOS
sdk\build_windows.cmd examples\mods\hello-overlay        # Windows, MinGW-w64 on PATH
```

Then copy the folder into the mods folder, turn it on in the launcher and allow its code. No game data in it.
