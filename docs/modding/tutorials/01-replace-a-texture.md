# Tutorial 01: replace a texture

In this tutorial you turn the **BURNOUT 3 TAKEDOWN** logo blue, on the title screen and in the corner of the menus,
see it in the game, and then package the change so it can be shared without any of the game's files in it. It takes
about twenty minutes and needs no programming: the asset tool `b3modtool` and any image editor.

What you will do:

1. get `b3modtool`;
2. list the textures of the menus' texture file, `DATA/FRONTEND.TXD`;
3. export them as PNG pictures;
4. edit one picture;
5. import the pictures into a new `FRONTEND.TXD`;
6. make a mod folder around it and check it with `b3modtool verify`;
7. turn the mod on and see it in the game;
8. do the same for the menus' small logo, in `DATA/GLOBAL.TXD`;
9. turn your changes into patches (`.b3patch`) for sharing: the game applies them to each player's own files.

Before you start: the port runs with your disc (a folder of its files or the disc image) and you have started the game
at least once. New to mods? Read [getting-started.md](../getting-started.md) first; it is short.

## 1. Get b3modtool

- **From a release**: `b3modtool.exe` (Windows) or `b3modtool` (Linux) is in the same folder as the launcher
  (`Burnout3Launcher.exe` / `b3launcher`). It is a command-line program: open a terminal in that folder (Windows:
  type `cmd` in the folder's address bar and press Enter; PowerShell users write `.\b3modtool.exe`, Linux users
  `./b3modtool`). The simplest is to work right there: the commands below make `work`, `blue-logo` and `share`
  folders beside the tool.
- **From the source**: `make modtool` builds `build/b3modtool` (Linux, macOS); the Windows build
  (`tools/native/build_win.sh RENDER=1`) makes `b3modtool.exe`.

Run it without arguments to see its commands. In the examples below, `<disc>` is your disc: the folder that holds
`SLUS_210.50` and the `DATA` folder (e.g. `C:\Games\Burnout3` or `~/Games/Burnout3`), or the disc image itself
(`~/Games/burnout3.iso`; a path inside an image like `~/Games/burnout3.iso/DATA/FRONTEND.TXD` works too). On Windows write
`\` or `/`, both work. With a source build, make an empty folder to work in and run the tool by its path from there.

## 2. List the textures

The game's own pictures live in two texture dictionaries: `DATA/FRONTEND.TXD` (the menus: logos, car and event
pictures, maps, icons) and `DATA/GLOBAL.TXD` (the race HUD and shared art).

```
$ b3modtool list <disc> '*.TXD'
     8517568  txd                DATA/FRONTEND.TXD
     1146112  txd                DATA/GLOBAL.TXD
2 files, 9663680 bytes
$ b3modtool txd list <disc>/DATA/FRONTEND.TXD
  0  advanced                      256x256  T8       levels 1 clut CT32 translucent
  1  Alaska                        128x256  T8       levels 1 clut CT32 translucent
  ...
203  titlescreen_logo              512x256  T8       levels 1 clut CT32 translucent
  ...
<disc>/DATA/FRONTEND.TXD: 221 rasters
```

Each line is one texture (a "raster"): its number, its name, its size in pixels, and how it is stored (`T8` means 256
colours from a palette). Number 203, `titlescreen_logo`, is the big logo on the title screen ("Press START button to
continue"), 512 x 256.

## 3. Export them as PNG

```
$ b3modtool txd export <disc>/DATA/FRONTEND.TXD work/fe
txd export: 222 files into work/fe (edit the PNGs at their size; then txd import)
```

`work/fe` now holds one PNG per texture, named by number and name (`203_titlescreen_logo.png`), and `txd.txt`, a list
of what each file is. Open a few: this is how you find the picture you want to change. These files are the game's
pictures: keep them on your PC, never share them, and keep `work` outside the mod folder you make in step 5
(`b3modtool pack` packs every file of a mod folder; `verify` and `pack` stop with an error when they find an export
folder inside a mod: `... a txd export (work/fe/txd.txt and 221 PNGs) ...`).

## 4. Edit the picture

Open `work/fe/203_titlescreen_logo.png` in any image editor (GIMP, Krita, Paint.NET, Photoshop, ...) and change it.
For this tutorial, make the red letters blue: in most editors a hue shift (GIMP: Colors > Hue-Saturation; Paint.NET:
Adjustments > Hue / Saturation), or swapping the red and blue channels, does it. Then save it over the same file, as
PNG.

The rules:

- **Keep the size** (512 x 256 here). The import refuses a picture of another size.
- **Keep the file name's number** (`203_`): it says which texture the PNG is. The rest of the name does not matter.
- **Transparency counts.** The logo has a see-through background; keep it. The game stores alpha in 129 steps, so
  very small alpha differences may round.
- This texture has 256 colours. If your picture has more, the import picks the best 256 and says so (`note:
  titlescreen_logo: 65536 colours reduced to 256`). Smooth gradients may band a little; that is the PS2's format.

You can leave every other PNG in the folder as it is: textures whose PNG you did not change are written back
unchanged.

## 5. Import

Make the mod's folder and import into it:

```
$ mkdir -p blue-logo/data/DATA
$ b3modtool txd import <disc>/DATA/FRONTEND.TXD work/fe blue-logo/data/DATA/FRONTEND.TXD
txd import: 221 PNGs, 1 rasters changed, 1 with a new palette -> blue-logo/data/DATA/FRONTEND.TXD (8517568 bytes, the original's size)
```

(Windows `cmd`: `mkdir blue-logo\data\DATA`.) The import reads the original file and your folder and writes a new
dictionary of exactly the same size and layout in which only the changed pictures differ. "1 with a new palette":
blue was not among the logo's 256 colours, so the import made a new palette for it. If something cannot be written
exactly, the import stops with a message and writes nothing.

## 6. The mod folder

A mod is a folder with a `mod.ini` and a `data` folder that holds files at their paths on the disc. Create
`blue-logo/mod.ini` with a text editor:

```ini
[mod]
id = blue-logo
name = Blue title logo
version = 1.0
author = you
description = The title screen's logo in blue.
api = 1
```

The folder is now:

```
blue-logo/
    mod.ini
    data/
        DATA/
            FRONTEND.TXD
```

Check it:

```
$ b3modtool verify blue-logo --game <disc>
  warning  data/DATA/FRONTEND.TXD: 99% of it is the disc's DATA/FRONTEND.TXD (ship a b3patch, or only what you made: b3modtool diff; pack refuses it without --force)
  note     no README: say what the mod changes, how to install it and whose work it uses
  note     no LICENSE: say what others may do with your work
verify: OK blue-logo: blue-logo 1.0, 2 files (1 disc paths, 0 patches), 0 errors, 1 warnings
```

`verify: OK` means the game will load it. The warning is about sharing: your `FRONTEND.TXD` is mostly the game's own
file, so it is fine on your PC but must not be shared like this (step 9 fixes that). Add a `README.txt` saying what
the mod does, and a `LICENSE` if you plan to share it.

## 7. See it in the game

Put the mod into the game's mods folder and turn it on:

1. In the launcher, open the **Mods** page and press **Open mods folder**. Copy the whole `blue-logo` folder into it
   (so that `mods/blue-logo/mod.ini` exists). The folder is `%APPDATA%\b3port\native\mods` on Windows,
   `~/.local/share/b3port/native/mods` on Linux, `~/Library/Application Support/b3port/native/mods` on macOS.
2. Press **Look again**. "Blue title logo" appears, OFF. Press **Turn on**.
3. Start the game. After the intro movie, the title screen shows the logo in blue. (The small logo in the corner of
   the menus is still red: it is another texture, step 8.)

From a terminal you can also run the game with exactly this mod, wherever the folder is:

```
native_play --game <disc> --mods blue-logo --mods-dir <the folder that holds blue-logo>
```

The game's log (the launcher's Play page shows the last run's; the files are in `logs/` beside the mods folder) names
the mod and, when the game opens the file, where it came from:

```
native_play: mods: active (mods.ini; the first wins): blue-logo 1.0 (1 file); set hash ...
mods: DATA/FRONTEND.TXD from blue-logo (8517568 bytes)
```

Not seeing it? Check that the log has those lines (no `mods:` lines: the mod is off or in the wrong folder; the Mods
page shows errors in red), and that you edited the right texture number.

To undo the mod: **Turn off** on the Mods page. Your disc was never changed.

## 8. The menus' logo too

The small logo at the top left of every menu ("SELECT OPTION") is not in `FRONTEND.TXD`: it is `B3Logo`, number 4 in
`DATA/GLOBAL.TXD` (256 x 64). The same steps, into the same mod:

```
$ b3modtool txd list <disc>/DATA/GLOBAL.TXD
  ...
  4  B3Logo                        256x64   T8       levels 1 clut CT32 translucent
  ...
$ b3modtool txd export <disc>/DATA/GLOBAL.TXD work/global
txd export: 236 files into work/global (edit the PNGs at their size; then txd import)
  ... make the red letters of work/global/004_B3Logo.png blue, as in step 4 ...
$ b3modtool txd import <disc>/DATA/GLOBAL.TXD work/global blue-logo/data/DATA/GLOBAL.TXD
txd import: 235 PNGs, 1 rasters changed, 1 with a new palette -> blue-logo/data/DATA/GLOBAL.TXD (1146112 bytes, the original's size)
$ b3modtool verify blue-logo --game <disc>
  warning  data/DATA/FRONTEND.TXD: 99% of it is the disc's DATA/FRONTEND.TXD (ship a b3patch, or only what you made: b3modtool diff; pack refuses it without --force)
  warning  data/DATA/GLOBAL.TXD: 99% of it is the disc's DATA/GLOBAL.TXD (ship a b3patch, or only what you made: b3modtool diff; pack refuses it without --force)
  ...
verify: OK blue-logo: blue-logo 1.0, 3 files (2 disc paths, 0 patches), 0 errors, 2 warnings
```

(236 files: some `GLOBAL.TXD` textures have smaller mip levels, exported as `.mip1.png` ...; leave those alone.) Copy
the mod into the mods folder again (replace the old copy) and start the game: the title logo and the menus' logo are
both blue, and the log has a line for each file, e.g. `mods: DATA/GLOBAL.TXD from blue-logo (1146112 bytes)`.
`GLOBAL.TXD` is also the race's texture file (the HUD); textures you did not change look exactly as before.

## 9. Share it as patches

Your `data/DATA/FRONTEND.TXD` is 8.5 MB of the game's own pictures with one changed. Sharing it would be sharing the
game. Instead, share **patches**: small files holding only your changes, which turn each player's own files into yours
([b3patch.md](../b3patch.md)).

Make a copy of the mod for sharing, with one patch per file in a `patches` folder, at the file's disc path plus
`.b3patch`:

```
$ mkdir -p share/blue-logo/patches/DATA
$ cp blue-logo/mod.ini share/blue-logo/
$ b3modtool diff <disc>/DATA/FRONTEND.TXD blue-logo/data/DATA/FRONTEND.TXD share/blue-logo/patches/DATA/FRONTEND.TXD.b3patch
diff: ... 748 ops, 8461373 bytes copied from the original, 56195 new; patch 59279 bytes
$ b3modtool diff <disc>/DATA/GLOBAL.TXD blue-logo/data/DATA/GLOBAL.TXD share/blue-logo/patches/DATA/GLOBAL.TXD.b3patch
diff: ... 316 ops, 1134032 bytes copied from the original, 12080 new; patch 13380 bytes
$ b3modtool verify share/blue-logo --game <disc>
verify: OK share/blue-logo: blue-logo 1.0, 3 files (0 disc paths, 2 patches), 0 errors, 0 warnings
$ b3modtool pack share/blue-logo blue-logo-1.0.b3mod --game <disc>
pack: blue-logo-1.0.b3mod (3 files)
```

(Windows `cmd`: `copy blue-logo\mod.ini share\blue-logo\`.) The `FRONTEND.TXD` patch is about 60 KB instead of
8.5 MB, and it only works with the file it was made from, which each player has on their own disc. (Your numbers
will differ with your picture.) `patches` is the folder the game looks in by default; `mod.ini` needs no new line
for it. `pack` writes a `.b3mod`, a zip with `mod.ini` at the top; it runs `verify` first and refuses a mod that holds
a copy of a game file, or (with `--game`) a whole game file that is mostly the disc's, like the `blue-logo` folder of
step 8: try `b3modtool pack blue-logo test.b3mod --game <disc>` and it says to ship patches instead.

**What your players do:** install `blue-logo-1.0.b3mod` with **Add mod...** on the launcher's Mods page (or unzip it
into the mods folder) and turn it on, as in step 7. Nothing else: when the game starts it applies each patch to the
player's own file, after checking that it is exactly the file you made the patch from, and keeps the result in the
mods folder's `.cache` folder for the next start. The log says so:

```
native_play: mods: blue-logo 1.0 ("Blue title logo") from ...: 2 files (2 from patches), mod hash ...
mods: DATA/FRONTEND.TXD from blue-logo (8517568 bytes, its patch applied to your disc's file)
```

A player whose disc has another version of a file (another region, or a file some other tool changed) gets the mod
turned off for that run, with the reason on the Mods page and in the log (`... was made from another version of
DATA/FRONTEND.TXD than your disc's ...`); their game is not changed. To see what a patch gives without the game,
`b3modtool apply <disc>/DATA/FRONTEND.TXD patches/DATA/FRONTEND.TXD.b3patch check.TXD` writes the result to a file.

You can try the share copy yourself: turn off (or remove) the `blue-logo` folder of step 7, put `share/blue-logo`
into the mods folder instead, and start the game. Both logos are blue, made from patches alone.

## What next

- Any texture in `FRONTEND.TXD` or `GLOBAL.TXD` works the same way: car pictures (`COMPCAR1` ...), event and track
  pictures, the world map, icons; `GLOBAL.TXD` holds the race HUD. Rasters with several palettes (the car-colour
  pictures) keep their palettes: paint them with the colours already in the picture.
- Textures on the tracks and the cars themselves are not importable yet (they are inside the track and car files;
  mod kit part 3c).
- The details of `txd import` (palettes, mip levels, alpha): [b3modtool.md](../b3modtool.md#txd-list--export--import);
  how the files are laid out: [file-formats/txd.md](../file-formats/txd.md).
- Next tutorial: [02: change menu text](02-change-menu-text.md).
