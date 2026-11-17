# Tutorial 02: change menu text

In this tutorial you rename the main menu's **SINGLE EVENT** to **QUICK RACE** and change its help line (the text
that scrolls along the bottom of the screen), see it in the game, and learn how the same steps make a whole translation. About ten minutes; you need
`b3modtool` and a text editor.

What you will do:

1. export the game's text to a text file;
2. find the lines you want to change;
3. write a small text file with only your changes;
4. import it into a new string table, in a mod folder;
5. check the mod and see it in the game.

If you have not done [tutorial 01](01-replace-a-texture.md), read its step 1 (getting `b3modtool`) and the
[getting-started](../getting-started.md) page first. `<disc>` below is your disc folder or image, as in tutorial 01.

## 1. Export the text

All the game's words on the US disc are in `DATA/GLOBALUS.BIN` (4,006 strings: menus, messages, hints, event and car
names, the credits); `DATA/HEADUS.BIN` holds 11 big headings. Export the main table:

```
$ b3modtool strings export <disc>/DATA/GLOBALUS.BIN work/strings.txt
strings export: 4006 strings -> work/strings.txt
```

`work/strings.txt` is plain UTF-8 text, one string per line, `<id> = <text>`:

```
# b3modtool strings: GLOBALUS.BIN, 4006 strings
...
229 = BURNOUT 3 WORLD TOUR
230 = SINGLE EVENT
231 = MULTIPLAYER
232 = DRIVER DETAILS
233 = RACE AND CRASH AROUND THE WORLD
234 = SET UP YOUR OWN RACE OR CRASH EVENTS
...
```

The **id** is the string's number. The game's code asks for strings by number, so the numbers never change: you only
change the text after `=`.

## 2. Find the lines

Search the file for the words you see on screen (your editor's Find, or `grep -n "SINGLE EVENT" work/strings.txt`).
The same words may appear more than once, for different screens; the main menu's items are 229 to 232, and 233 to 237
are their help lines, which scroll along the bottom of the screen while the item is selected. Here: 230 is the menu
item, 234 its help line.

## 3. Write your changes

Make a new text file, `work/my-strings.txt`, with only the lines you change:

```
# my changes to the main menu
230 = QUICK RACE
234 = PICK A TRACK AND A CAR, THEN GO
```

You could also edit the whole exported file, but a file with only your lines is easier to read, to update, and to
share: the ids you leave out keep the game's text.

Rules for the text:

- Text may be longer or shorter than the original; the file is rebuilt around it. But menu boxes are not: very long
  text runs past the box or is cut. Check every change in the game.
- Escapes: `\n` starts a new line, `\\` is a backslash, `\uXXXX` any character by its code.
- Letters the game's font does not have are drawn as a wrong symbol. Safe are ASCII and the characters the game's own
  text already uses: `À Á Â Ä Å Ç È É Ê Ë Ì Í Î Ï Ò Ó Ô Õ Ö Ù Ú Û Ü ß à á â ä ç è é ê ë ì í î ï ò ó ô õ ö ù ú û ü ÿ
  Œ œ Ÿ Ń ń × £ © ® ™ ½ ’ “ ” …`. Anything else (`ñ`, for one) may or may not be in the font: try it in the game
  ([file-formats/strings.md](../file-formats/strings.md#characters-and-markup-seen-in-the-text)).
- Keep placeholders like `%1K` where the sentence needs them: the game fills them in.

## 4. Import into a mod folder

```
$ mkdir -p menu-words/data/DATA
$ b3modtool strings import <disc>/DATA/GLOBALUS.BIN work/my-strings.txt menu-words/data/DATA/GLOBALUS.BIN
strings import: 2 strings changed -> menu-words/data/DATA/GLOBALUS.BIN (214836 bytes)
```

A mistake in the text file stops the import with the line number and writes nothing:

```
b3modtool: work/my-strings.txt: line 2: expected "<id> = <text>"
b3modtool: work/my-strings.txt: line 3: id 230 given twice
```

Then `menu-words/mod.ini`:

```ini
[mod]
id = menu-words
name = Menu words
version = 1.0
author = you
description = SINGLE EVENT on the main menu reads QUICK RACE.
api = 1
```

## 5. Check it and see it in the game

```
$ b3modtool verify menu-words --game <disc>
  warning  data/DATA/GLOBALUS.BIN: 93% of it is the disc's DATA/GLOBALUS.BIN (ship a b3patch, or only what you made: b3modtool diff; pack refuses it without --force)
  note     no README: say what the mod changes, how to install it and whose work it uses
  note     no LICENSE: say what others may do with your work
verify: OK menu-words: menu-words 1.0, 2 files (1 disc paths, 0 patches), 0 errors, 1 warnings
```

Copy the `menu-words` folder into the mods folder, press **Look again** on the launcher's Mods page, **Turn on**, and
start the game (or `native_play --game <disc> --mods menu-words --mods-dir <folder>`). Load your profile: the main
menu's second item reads QUICK RACE, and when it is selected the line scrolling along the bottom reads PICK A TRACK AND
A CAR, THEN GO. The log has `mods: DATA/GLOBALUS.BIN from menu-words`.

Two mods that both replace `GLOBALUS.BIN` do not combine: the one higher on the Mods page wins the whole file.

A text mod does not change how the game plays. One side effect was seen in our checks: with this text, the race's
opening fly-by starts from a different camera than without the mod (the race itself is the same). Recordings and bug
reports name the mods a run had, so this never mixes up a comparison.

## Sharing

As in tutorial 01, the `GLOBALUS.BIN` you made is mostly the game's text: do not share it. Share one of:

- **your text file** (`my-strings.txt`): it holds only words you wrote. Players run the `strings import` of step 4
  themselves, on their own disc;
- **a patch**: `b3modtool diff <disc>/DATA/GLOBALUS.BIN menu-words/data/DATA/GLOBALUS.BIN
  share/menu-words/patches/DATA/GLOBALUS.BIN.b3patch`, packed with `b3modtool pack` (tutorial 01, step 9). Players
  install the `.b3mod` and turn it on: the game applies the patch to their own `GLOBALUS.BIN` when it starts.

## A whole translation

A translation is the same mod with every line changed:

1. Export `GLOBALUS.BIN` (and `HEADUS.BIN` for the 11 headings: `b3modtool strings export <disc>/DATA/HEADUS.BIN
   work/head.txt`).
2. Translate the text after each `=`, keeping the ids, the `\n` line breaks and the `%1K`-style placeholders. Delete
   lines you have not translated yet: they stay in English, so you can test as you go.
3. Import each file into `my-language/data/DATA/GLOBALUS.BIN` and `.../HEADUS.BIN` (the US game always loads these
   two names, whatever the language).
4. Play through the menus: translated text is often longer, and the boxes do not grow.

Limits: the game loads `GLOBALUS.BIN` into a fixed block of 268,288 bytes; the US text leaves about 53 KB free (roughly
26,000 more characters). `HEADUS.BIN` has about 1.7 KB free. Characters outside the font (above) cannot be shown
without new font art, which is not moddable yet. Share the translation as your text files or a patch, never the
`.BIN` files. Details: [file-formats/strings.md](../file-formats/strings.md).

## What next

- Every command's options: [b3modtool.md](../b3modtool.md#strings-export--import).
- Previous tutorial: [01: replace a texture](01-replace-a-texture.md). All the modding pages: [README](../README.md).
