# Mods: getting started

What works today (mod kit parts 1, 1b and 3a, see [DESIGN.md](DESIGN.md)): **data mods**, and the tool to make them.
A mod is a folder with a `mod.ini` and a `data` folder of files at their disc paths; when the game opens one of those
paths it gets the mod's file instead of the disc's, and so does the renderer for the files it reads itself (tracks,
cars, textures). Your disc (its image or folder) is never changed. The asset tool, [`b3modtool`](b3modtool.md), turns
the game's textures into PNG and back and its text into a text file and back, and packs and checks mods. Every key of
`mod.ini` and the other files: [mod-format.md](mod-format.md). Tuning presets and the tuning console: [tuning.md](tuning.md).
Code mods are the next part.

All the modding pages: [README.md](README.md). Step by step, on your own disc:
[tutorial 01, replace a texture](tutorials/01-replace-a-texture.md) and
[tutorial 02, change menu text](tutorials/02-change-menu-text.md).

## Installing and ordering mods

1. In the launcher, open the **Mods** page and press **Open mods folder**. It is `mods` in the game's user data
   folder:
   - Windows `%APPDATA%\b3port\native\mods`
   - Linux `~/.local/share/b3port/native/mods`
   - macOS `~/Library/Application Support/b3port/native/mods`
2. Press **Add mod...** and choose the mod's `.b3mod` or `.zip` file: the launcher checks it and unpacks it into its
   own folder there. An archive that would write outside that folder, holds links, is damaged or has no valid
   `mod.ini` is refused, and nothing is written. If the mod is installed already, the launcher asks before replacing
   it. **Add folder...** does the same for a mod's folder (copied in). A pack of the game's own folders (`PVEH`,
   `TRACKS`, `DATA`, ... at its top, or in its one top folder) with no `mod.ini` is offered as a file-replacement
   mod: after you say yes, its folders go into the new mod's `data` folder and the launcher writes a `mod.ini` for it
   ([mod-format.md](mod-format.md), "Archives"). (Or unpack it yourself so that `mods/<some folder>/mod.ini` exists,
   then press **Look again**.)
3. **Turn on** the mods you want. New mods are off.
4. Order them with **Move up / Move down**: when two mods have the same file, the one higher in the list wins. The
   page counts those files at the top ("Conflicts"), and each mod's card lists the files where it wins over another
   mod and those where a mod above hides its own.
5. Start the game. The changes apply from the next start.

A mod can need another one (`[requires]` in its `mod.ini`): its card says "Requires: ...", and if that mod is not
installed, not on, or too old, the mod stays off at the next start and its card says why. A mod that changes another
mod's files should be above it in the list.

If the game did not close cleanly twice in a row, it starts once in **safe mode**: low settings and **no mods** (the
window says so). The next start uses your mods again. Starting in safe mode yourself (the launcher's "start in safe
mode", or `--safe-mode`) also leaves the mods off.

The launcher keeps the order and the switches in `mods.ini` beside the mods folder. The game's log names the mods it
ran with and a hash of their files (`native_play: mods: active ...`); a bug report carries it, so please keep mods on
when you report something that only happens with them (and say so). With no mods on, the log says
`native_play: mods: none` and the game is exactly the one on your disc.

From a terminal: `native_play --mods id1,id2` runs with exactly these mods (the first wins), `--mods none` with none,
`--mods-dir <folder>` takes the mods from another folder. A mod named there that is missing, cannot be used or lacks
one of its requirements ends the run with a message (exit code 2).

**Mods that contain code.** A mod whose card says "Contains code" carries a library the game runs as a program on your
PC. When you turn such a mod on (or start the game with it on), the launcher asks once whether you allow its code;
allow code only from people you trust, ideally with its source. A change to any library in the mod is asked about again.
Its settings, if it has any, are on PC SETTINGS > MODS in the game and under its card on the Mods page; the card also
says whether it loaded at the last start, and why not (for example when two mods change the same game function).

## Making a data mod

```
my-mod/
    mod.ini
    data/
        DATA/FRONTEND.TXD          replaces the disc's DATA\FRONTEND.TXD
    README.txt                     for people: what it does, how to install, credits, licence
```

`mod.ini`:

```ini
[mod]
id = my-mod                  # a-z, 0-9, '.', '_', '-'; unique
name = My mod
version = 1.0
author = you
description = One line saying what it changes.
api = 1                      # the mod API version this was made for
priority = 0                 # optional: where it joins the list when installed (higher: nearer the top)

[files]
root = data                  # optional: the folder with the disc paths (default "data")

[requires]                   # optional: other mods this one needs on
base-pack = 1.2              # version 1.2 or newer ("" or "*": any version)
```

- Paths below `data` are the disc's paths; upper or lower case does not matter (`data/data/frontend.txd` works).
- A mod may add new paths too; the game only opens names its code asks for.
- Not replaceable by files: `SLUS_210.50` and `SYSTEM.CNF`. The port does not run the disc's program: it runs the
  game's code natively, so **PS2 patches to the executable (pnach codes, a patched ELF) do nothing here**. Behaviour
  changes need a code mod ([the SDK](../../sdk/README.md); [tutorial 03](tutorials/03-a-settings-row.md), [the SDK reference](sdk-reference.md)).
- Left out with a note in the log: files that would lead out of the mod folder (links), two files whose names differ
  only in case, dot files and `Thumbs.db` / `desktop.ini`.

Check a mod: turn it on, start the game, and look at the log (the launcher's Play page shows the last run's; the logs
are in `logs/` beside the mods folder):

```
native_play: mods: my-mod 1.0 ("My mod") from .../mods/my-mod: 1 files, mod hash 5a1f...
native_play: mods: active (mods.ini; the first wins): my-mod 1.0 (1 file); set hash 77c2...
mods: DATA/FRONTEND.TXD from my-mod (8517568 bytes)
```

The last line appears when the game first opens that file. Notes about the manifest (a missing `api`, an unknown key)
are printed before it and shown on the Mods page. The renderer prints the same set when it reads files of it itself
(`[b3render] mods: my-mod 1.0 (1 file); set hash 77c2...`).

To share a mod, zip its folder (or its contents) and name the file `<id>.b3mod` or `.zip`: Add mod accepts `mod.ini`
at the top of the archive or in one top folder. Put your own files in it, not the game's (below).

## Editing the game's files

With [`b3modtool`](b3modtool.md) (`make modtool`; `b3modtool.exe` in the Windows build):

- **Textures** (`DATA/FRONTEND.TXD`, `DATA/GLOBAL.TXD`): `b3modtool txd export <disc>/DATA/FRONTEND.TXD work/fe` writes
  each texture as a PNG; edit them at their size in any image editor; `b3modtool txd import <disc>/DATA/FRONTEND.TXD
  work/fe my-mod/data/DATA/FRONTEND.TXD` writes the new file ([tutorial 01](tutorials/01-replace-a-texture.md)).
- **Text** (`DATA/GLOBALUS.BIN`: menus and messages): `b3modtool strings export <disc>/DATA/GLOBALUS.BIN strings.txt`,
  keep only the lines you change (`230 = MY EVENT`), then `b3modtool strings import <disc>/DATA/GLOBALUS.BIN strings.txt
  my-mod/data/DATA/GLOBALUS.BIN` ([tutorial 02](tutorials/02-change-menu-text.md)).
- Before sharing: `b3modtool diff <disc file> <your file> my-mod/patches/<disc path>.b3patch`, remove your full copy
  from the mod, then `b3modtool pack my-mod my-mod.b3mod --game <disc>`, which runs `b3modtool verify`: it refuses a
  mod with a copy of a game file in it, a whole game file with a small change (unless `--force`), and the export
  folders of b3modtool (unless `--allow-exports`). Players' games apply the patches to their own files when they start
  ([mod-format.md](mod-format.md) "Patches").
- **HD textures** in a mod: `[textures] pack = textures` in mod.ini and PCSX2-format replacement DDS files in that
  folder; they are drawn above the player's own HD pack ([mod-format.md](mod-format.md) "[textures]").

Textures inside tracks and cars, sounds, models and the tuning table are not importable yet (b3modtool.md "What can
be changed today").

What is in each file, how it is laid out and how far it can be modded today: [file-formats/index.md](file-formats/index.md)
(every file on the disc, one page per format; the tuning variables in
[file-formats/tuning-registry.md](file-formats/tuning-registry.md), their defaults in
[file-formats/vdb.md](file-formats/vdb.md)).

## Please share mods the right way

- Make mods from **your own copy** of the game.
- **Ship your own content, never the game's files.** A modified game file is still mostly the game's: ship a patch
  ([b3patch](b3patch.md), made with `b3modtool diff`) or only the parts you made (the PNGs you painted, the lines of
  text you wrote) (DESIGN.md §8). `b3modtool verify --game <disc>` checks it. The game applies a mod's patches to the
  player's own files when it starts.
- Credit and ask before using other people's work; add a LICENSE.
