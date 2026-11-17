# Tutorial 04: a tuning preset

In this tutorial you move the race camera further behind the car: first live, with the tuning console, then as a
**tuning preset** in a mod that anyone can install. About fifteen minutes; no tools beyond a text editor (and
`b3modtool` for the last check). What the console and presets are: [tuning.md](../tuning.md).

What you will do:

1. turn the tuning console on;
2. find the follow camera's variable and change it in a race;
3. save your change as a preset (or write the preset by hand);
4. look at the mod and check it;
5. turn the mod on and see it in the game;
6. learn what "changes gameplay" means for replays and ghosts, and share it.

A camera is a gentle first change: it changes what you see, not how the car drives, yet the game treats it like any
other tuning value. The same steps change boost rules, AI or car physics.

## 1. Turn the console on

Start the game, open PC SETTINGS (the main menu's last rows, or the pause menu in a race), go to the INTERFACE tab and
scroll down to its **DEVELOPER** section (the last one but CONTROLS). Set **DEVELOPER MODS** to On, close the page and
quit the game. (The launcher's Settings page has the same row.) The console is installed only when the game starts with
it on.

Start the game again. PC SETTINGS now has a **DEVELOPER** tab, the last one, and F9 opens the live overlay.

## 2. Find the variable and change it

Start a race (SINGLE EVENT > RACE, any track and car). When you are driving, press **F9**: a box at the left lists the
tuning variables the game has registered now, by group. The game sees no controller while the box is open, so stop on
a straight first, or just let the car roll.

- **Page Down** moves by group. Find the group `CAMERA/FOLLOW/` (the overlay's letters are all capitals).
- **Down** to `CAMERA OFFSET Z`: the distance of the camera along the car (the follow camera's offset is a vector;
  X, Y and Z are its three rows). The value is your car's own.
- **Left** / **Right** change it a step (Shift + Left / Right: ten steps). Press F9 to close the box and drive: the
  camera is further back (or nearer, the other way). Home puts back the first value.

The same in the DEVELOPER tab of PC SETTINGS: the group `Camera/Follow/`, the row `Camera Offset Z`; its help line
gives the full name, `Camera/Follow/ | Camera Offset`, its config file (one per car model, e.g.
`../Export/ValueDB/Camera/<car>.cfg`) and its key.

Every change you make goes into the session's recording (a `tune` line in the pad recording, an entry in the replay
file), so a replay of this session shows it too.

## 3. Save it as a preset

In PC SETTINGS > DEVELOPER, choose **SAVE AS PRESET** (Cross / Enter). The row's help line then names the new mod:
a folder `tuning-<date>-<time>` in your mods folder (the launcher's Mods page > Open mods folder):

```
tuning-20261112-201500/
    mod.ini
    tuning/preset.ini
```

**By hand instead.** The same mod can be written without the console. Make a folder `longer-camera` in the mods
folder with these two files:

```ini
# longer-camera/mod.ini
[mod]
id = longer-camera
name = Longer follow camera
version = 1
author = you
description = The race camera further behind every car.
api = 1

[tuning]
preset = tuning/camera.ini
```

```ini
# longer-camera/tuning/camera.ini
# every car's follow camera: Z (along the car) set to this value
Camera/Follow/ | Camera Offset[2] = <your value>
```

`<your value>` is the number you settled on in step 2 (keep its sign; twice the game's value is twice as far). Names are
the game's own, exactly as the console shows them in its help line: group, ` | `, name, and `[2]` for the third
element (Z). The line has no `[file]` section above it, so it reaches **every** car model; under a section like
`[../Export/ValueDB/Camera/SPRTCAR1.cfg]` it would reach one model only. Every name the game registers is listed in
[tuning-registry.md](../file-formats/tuning-registry.md).

## 4. Look at the mod and check it

Open the saved `tuning/preset.ini`. Each changed value is a line under its car's config file, with the key in a
comment:

```ini
[../Export/ValueDB/Camera/<car>.cfg]
Camera/Follow/ | Camera Offset[2] = <value>   # 0x<key>
```

A saved line sits under your car's config file, so it reaches that car model only. To give every car the same
camera, put `[]` (any file) above the line, or remove the section line. Edit it freely (several lines, other
variables); the format is in
[tuning.md](../tuning.md) "Presets". Give the mod a name of your own in `mod.ini` (`id`, `name`), then check it:

```
$ b3modtool verify <mods folder>/longer-camera
  (add --game <disc> to check that no file is a copy of the game's own)
  note     no README: say what the mod changes, how to install it and whose work it uses
  note     no LICENSE: say what others may do with your work
  note     tuning/camera.ini: a tuning preset of 1 line: the mod changes gameplay
verify: OK <mods folder>/longer-camera: longer-camera 1, 2 files (0 disc paths, 0 patches), 0 errors, 0 warnings
```

`verify` reads the preset too: a line it cannot read (no ` | ` in the name, no value, an element that is not a
number) is an error naming the line.

A name the game does not know is not an error: the preset simply never meets that variable. A value of the wrong kind
is reported when the variable registers (`native_play: tuning: ... is not a whole number`).

## 5. Turn the mod on and see it

The launcher's Mods page (Look again, if it was open) shows the mod with an amber line **"Changes gameplay: a tuning
preset (tuning/camera.ini)"**. Turn it on and play. You can turn DEVELOPER MODS off again: a preset needs no console.

In a race the camera is where you put it, from the race's start: the preset is written as each car registers its
camera, right after the game gives it its default. The run's log (the launcher's Play page shows the last
run's; the logs are in `logs/` beside the mods folder) shows it:

```
native_play: tuning: preset of longer-camera (tuning/camera.ini): 1 line; this mod changes gameplay
...
native_play: VBlank <n>: tuning: longer-camera registered: Camera/Follow/ | Camera Offset z (../Export/ValueDB/Camera/<car>.cfg) = <your value> (was <the game's>; longer-camera line 3)
```

## 6. Gameplay, replays, ghosts, sharing

A tuning preset changes gameplay **by nature**, even one that only moves a camera: the port does not judge what a
value does. So:

- the mod is marked on the Mods page, at the start of every run's log, in pad recordings (`gameplay tuning
  longer-camera`) and in replay files;
- a replay recorded with the mod on plays only with the same mods installed, preset file included (its bytes are in
  the mod set's hash): with another version of the preset the replay is refused rather than played wrong;
- ghosts and times made with such a mod belong to that mod set, not to the plain game.

To share the mod, pack it and give out the `.b3mod` file; it holds your text only, no game data:

```
$ b3modtool pack <mods folder>/longer-camera longer-camera.b3mod
pack: longer-camera.b3mod (2 files)
```

Others add it with the Mods page's **Add mod...** ([getting-started.md](../getting-started.md)).

## What next

- Other variables: boost (`Boost Bar | ...`), score rules (`Score/...`), AI (`AI | ...`), each car's physics
  (`Physics | ...`). [tuning-registry.md](../file-formats/tuning-registry.md) lists all 705 registrations with their
  ranges.
- Changing the defaults themselves (and the values whose names are not known) is a VDB.XML mod:
  [b3modtool.md](../b3modtool.md) "vdb".
