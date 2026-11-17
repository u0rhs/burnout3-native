# menu-label-setting: a settings row

A code mod (the mod kit's SDK): it adds the row QUICK RACE LABEL (Off / On) to PC SETTINGS > MODS; while it is On, the
main menu's SINGLE EVENT reads QUICK RACE. It wraps the game's string lookup (func_00271A00) and answers string 230 with
its own text in guest memory it allocated. [Tutorial 03](../../../docs/modding/tutorials/03-a-settings-row.md)
explains each line.

Build (the library lands in `bin/<system>/`, where `mod.ini` names it):

```sh
make -C sdk MOD=examples/mods/menu-label-setting              # Linux, macOS
sdk\build_windows.cmd examples\mods\menu-label-setting        # Windows, MinGW-w64 on PATH
```

Then copy the folder into the mods folder, turn it on in the launcher and allow its code. No game data in it.
