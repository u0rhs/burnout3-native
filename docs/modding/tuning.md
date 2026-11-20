# The tuning console and tuning presets

The game keeps its rules in **tuning variables**: boost and score rules, AI driving, every car's physics, the cameras,
sound mixing, pad rumble, the bloom per route. Each one is registered with the game's variable database when the game
starts or when a race is prepared, and gets its default from `DATA/VDB.XML` ([file-formats/vdb.md](file-formats/vdb.md);
every registration: [file-formats/tuning-registry.md](file-formats/tuning-registry.md)). The tuning console shows these
variables while the game runs and lets you change them; a **tuning preset** is a mod that sets them. What each one
does, tab by tab in the console's order: [tuning-options.md](tuning-options.md).

Mod kit part 4 ([DESIGN.md](DESIGN.md) §4). Code: `include/b3tune/tune.h` (the model: names, values, presets, events),
`tools/native/tuning_console.h` (the game's side, in native_play), `src/b3replay/tuneconsole.h` (the DEVELOPER tab and
the F9 overlay), `include/b3tune/browse.h` (the tabs, sections and pages both show), tests `tests/test_tune.cpp`,
`tests/test_tune_browse.cpp` and `tests/test_tune_changes.cpp` (the CHANGED tab and resets). Tutorial: [tutorials/04-a-tuning-preset.md](tutorials/04-a-tuning-preset.md).

**Changing a tuning value changes gameplay.** The console is off by default, and with it off the game runs exactly as
without it (nothing is installed). Every change you make is recorded, so replays of your session play the same, and a
mod with a preset is always marked as changing gameplay.

## Turning it on

PC SETTINGS > MODS > DEVELOPER (the last section but CONTROLS) > **DEVELOPER MODS**: On, then start the game again.
The launcher's PC SETTINGS page has the same row, and its Mods page's **Developer mods** button is the same switch (it
also lets code mods marked `platform = yes` hook the platform tier). From then on:

- PC SETTINGS has a **DEVELOPER** tab (the last one);
- **F9** opens and closes the live overlay, in a race or anywhere else in the game.

If F9 does nothing at all, the key may be a media key on your keyboard (laptops: hold **Fn**). Pressed without the
overlay in the run, F9 shows a short notice saying how to turn it on (DEVELOPER MODS off, or turned on since the
game started: start it again).

The log says which it is when the page first opens: `[b3tune] DEVELOPER MODS on: the DEVELOPER tab and F9 are there`,
or `... no tuning console in this run (it is installed at the start: start again)`. Turn it off the same way (from
the next start). Offscreen runs (tests, the golden checks) never take the setting:
`native_play --tuning on` (or `B3NATIVE_TUNING=1`) turns the console on for one run, `--tuning off` keeps it off in a
window run.

## How it works

native_play wraps the game's three registration functions, `GtComms::CGtCommsDatabase::AddEntry` for floats
(0x0021BB60), ints (0x0021BD80) and vectors (0x0021B9B0), on the port's layer of the hook table
([hooks.md](hooks.md)). Each call runs as it always does; afterwards the wrapper reads the registry entry the game just
filled (the variable's address, its name, group and config file, its range, its element count, its key) and keeps it,
in registration order. It reads; it writes only for a preset or an edit (below). It also wraps
`CGtCommsDatabaseGame::InitialiseDefaultValues` (0x0024FE90, the defaults applied when VDB.XML is loaded), to apply
presets to the variables registered before that.

At the end of each run the log has one line with what was registered:

```
native_play: tuning: 1088 AddEntry calls (float 996, int 55, vector 37; 0 with their config file disabled), 1083 variables, 1081 keys, 692 names; defaults loaded 1 time(s); 0 preset writes; 0 edits (0 writes)
```

(race_check.sh's race scenario: the boot, the front end, one race on US/C2_V2 [C tune4, 2026-11-12].) The number of
calls is not the 705 of the [registry table](file-formats/tuning-registry.md): that table counts **call sites** in the
code. At run time the static variables (score, boost, AI, rumble, the sound managers) register once, while every car,
camera, sound object and route registers its own each time a race is prepared, once per vehicle on the road: in that
run 430 of the calls are `VehiclePhysics/<car>.cfg` (64 per racing car, 9 per traffic model), and 327 variables were
registered before VDB.XML was loaded. Code a run never reaches registers nothing. `B3NATIVE_TUNE_LOG=1` logs every
registration with its name, file, type, value, range and key (the values are your disc's: keep such logs to
yourself).

A variable is **live** while the game has it registered (the registry entry still holds it). Per-race variables are
unregistered when the race ends; the console only shows and edits live ones.

## Names and keys

A variable is named as `b3modtool vdb export` names it ([b3modtool.md](b3modtool.md) "vdb"): `<group> | <name>`, with
its config file apart. One element of an array is `<group> | <name>[i]`; a vector's elements are `[0]` (x), `[1]` (y)
and `[2]` (z) (the fourth, w, is the authoring tool's fill value and is left alone). Examples (the names are the
game's, from the code; the values are not shown here):

```
Score/Boost/Air | Minimum Air Distance (Metres)               in ../Export/ValueDB/Score.cfg
Boost Bar | Boost Rate (Boost Units per Second)[2]             an array of 4
Camera/Follow/ | Camera Offset[2]                              a vector, z; one per car model
```

Every variable also has a **key**: `CGtHash(name + group + "/" + file)`, the number VDB.XML files its default under
([vdb.md](file-formats/vdb.md) "The key"), written `0x<8 hex digits>`. Per-car variables (Physics, Camera,
VehicleAudio ...) have the same `<group> | <name>` for every car and one key per car model: the name reaches all of
them, the key one model.

## Finding a value

A race registers about 1,100 variables. Both views (the F9 overlay and the DEVELOPER tab) sort them the same way:
**tabs** by the kind of change, then **sections**, then **subsections**, then the values. No page is longer than a few
dozen rows (the longest in a race: 34).

| Tab | What is in it |
|---|---|
| YOUR CAR | The car you are driving now (below: "The car page"). |
| CHANGED | Every value that differs from its default **now**, each with its default; RESET ALL (below: "What you changed"). |
| MOST USED | Your **favourites**, the values you **changed last** (a history: the newest first, up to 12, a reset included) and a short **starter list** of the most interesting ones. |
| HANDLING | Steering, drift, suspension front / rear, body roll, the differential (LSD), mass, downforce and in-the-air values; per car model. |
| ENGINE & SPEED | Top speed in boost, acceleration, braking, drag, the engine (torque, revs, shifts), the gear ratios, the boost kick; per car model. |
| BOOST & SCORE | The boost bar, the boost each move earns (near misses, drifts, takedowns ...), burnout points (driving, takedowns & slams, by category) and the other score rules. |
| DAMAGE & CRASHES | How a car reacts to being hit, wall hits, the crash action (air ram) forces. |
| AI RIVALS | How the rivals drive: avoidance, targets, aggressive driving and slams. |
| TRAFFIC | The traffic models' mass and suspension, and the traffic's sounds. |
| CAMERA | The race cameras of each car, the crash camera, the camera shake effects. |
| AUDIO | Skids, the engines, boost, props being hit, the HUD's sounds, the mix. |
| SURFACES | The sound of each road surface (tarmac, gravel, snow, metal, wood ...). |
| VISUALS | Bloom per route, the cars' paint and glass. |
| RUMBLE | Controller rumble: surfaces, kerbs, boost, collisions, crashes. |
| OTHER | Anything no rule places, under its own group (empty for every registration the code makes). |

The per-car values (VehiclePhysics, Camera, VehicleAudio, VehicleMaterial) have a subsection per car model; the model
you drive comes first, marked `(your car)`. A section with only one subsection shows its values directly ("Follow -
COMPCAR1 (your car)"). A value's help line gives its full name (`<group> | <name>`), its config file, its type and
range. What each value does, under the same tab, section and subsection: [tuning-options.md](tuning-options.md).

The place of each variable is worked out from the strings the game registers it with (its group, name and config
file) by a table of rules in `include/b3tune/browse.h` (`rules()`): patterns on the group, name and file, and the tab,
section and subsection they give. A traffic model's physics (its file has no `Physics/Race Car/...` values) goes to
TRAFFIC. With a race's registry (1,081 variables, 1,356 values) the tabs hold: HANDLING 175 variables, ENGINE & SPEED
115, BOOST & SCORE 87, DAMAGE & CRASHES 37, AI RIVALS 73, TRAFFIC 159, CAMERA 40, AUDIO 199, SURFACES 120, VISUALS 29,
RUMBLE 47, OTHER 0 (race_check.sh's race on US/C2_V2 [C tunenav1, 2026-11-14]). `make test`
(tests/test_tune_browse.cpp) places each of the 705 registrations of the
[registry table](file-formats/tuning-registry.md) and checks that none is left to OTHER.

**Search** (the overlay, a keyboard): **/** or **Ctrl+F**, then type any part of a name (several words: all of
them); the results come from every tab, each with the tab it lives in. **Enter** on one goes to its place; Left / Right
change it where it is listed; Backspace on an empty search or Escape ends the search.

**Favourites**: **F** (a controller: **Triangle**) on a value pins or unpins it; pinned values have a `+` before their
name. Favourites and the recently changed list are kept in `tuning_console.ini` beside your PC settings (the user data
folder), one line per value: `<group> | <name> [<element>] @ <config file>`. A value of a per-car file is the
favourite of that car model.

## What you changed

Edits are easy to lose track of in a dozen tabs, so the console keeps the current state for you.

**The default** of a value is what the game gave it: its VDB.XML default, with the presets of the mods you have on
(a preset mod is part of the game you chose to play). native_play takes it as the variable registers (and again after
the defaults are loaded), before your session's edits are written over it. A value is **changed** while you have edited
it and it differs from that default; stepping it back to its default by hand makes it unchanged again. A value the
game itself changes (never edited) is not a change of yours.

**The CHANGED tab** (the second, right after YOUR CAR, so one Tab / R1 from where the overlay opens) lists every
changed value, live:

- **Reset all** first (Enter / Cross asks before it does anything, below);
- the car page's rows still in effect: **Freeze damage** and **Infinite boost** while on (their default is OFF), and
  **Boost use rate** while the car still has the rate you set (the game's own comes back by itself when the bar
  changes size or a new race starts; the default shown is the rate it had before your change). Boost and health are
  the race's state, not settings: they are never listed;
- then the values by tab in the tabs' order, each with its page (the car model or section) after its name, its value
  now and `(default <x>)`;
- last, values of an earlier race that are not registered now (a car model you are not driving): the edit is kept and
  written again when the variable registers (the next race in that car), so they stay listed and can be reset.

With nothing changed the page says **Nothing changed: every value is the game's own.** Enter (Cross) on a row goes to
its place in its tab (a car row: to its row on YOUR CAR); R (Square) on it puts it back.

**Markers and counts**, in both views: a changed value is drawn in the accent colour (green), with `*` before its name
and `(default <x>)` after its value; a tab, section or subsection holding changed values shows how many, e.g.
`[HANDLING (3)]` in the tab strip and `Steering (2)` on the tab's page, so you can follow your edits down from the top.
The favourites' `+` stays.

**Resets**, on every page (a tab, CHANGED, MOST USED's lists, a search):

| Keyboard | Controller | Puts back |
|---|---|---|
| R or Home (in a search: Home) | Square | the focused value (a car row: FREEZE DAMAGE / INFINITE BOOST off, the game's boost use rate) |
| Shift+R or Shift+Home | R2 held + Square | RESET SECTION: the changed values of the page you are on: a section with its subsections, a tab's top page the whole tab, YOUR CAR its rows, a list page what it lists |
| Ctrl+R or Ctrl+Home | Select | RESET ALL: every changed value and the car page's three |

RESET SECTION of more than one value and RESET ALL ask first, in place of the help line: `RESET ALL: 3 CHANGED VALUES
BACK TO THEIR DEFAULTS?` **Enter** (or Y; a controller: **Cross**) does it; **any other key** (or button) answers no
and nothing is reset, so one key press never wipes a session. Closing the overlay answers no as well. The help line
then says what was done until the next key. The key lines at the bottom follow the device you used last (a keyboard's
keys, or the controller's buttons once you press one).

A reset is an edit like the others (below: "Edits are recorded"): native_play takes it at the next VBlank, writes
each changed value's default to it and records an event with the default's raw word, so replays play it exactly;
it logs `tuning: <name> = <value> (the default) (<file>) on <n> variables`, and RESET ALL first
`tuning: reset all: <n> values back to the default`. A reset value is again what the game had: SAVE AS PRESET leaves
it out.

## The car page

YOUR CAR, the first tab, is the car you are driving now. Its values are read on the game's thread at each VBlank
(shown at most ten times a second); its changes are edits like the others: written at the next VBlank, recorded,
replayed (below). Outside a race the rows show `-` and nothing is written.

| Row | What | Where it is in the game |
|---|---|---|
| Speed | The speedometer's value, mph or km/h as the HUD shows it (and m/s); read only | the W lane at 0x0055EF70 + 0x1CB80 + 0xB0, times the HUD's unit factor ([L] CB3HUDSpeedo::Update) |
| Boost | The boost in the bar; Left / Right a twentieth of the bar | the race car's CB3Burn (car + 0x118C) + 0x38 ([C] CB3Burn::AddBurn / RemoveBurn / Update) |
| Boost bar size | The bar's size now (it grows with extra segments); read only | CB3Burn + 0x34 |
| Boost use rate | Boost units used per second of boost (the game sets it again when the bar changes size) | CB3Burn + 0x40 ([L] CB3Burn::Update, SetNumExtraBoostBarSegments) |
| Health | 100 % is full; only Road Rage uses it (a race there ends at 0) | the car's CB3Score (car + 0x10C0) + 0x5F4 = car + 0x16B4 ([C] CB3Score::DecrementHealth 0x00156220, CB3StageLogic::ResetCarDamage 0x00395A00) |
| Repair | Health back to full (Enter / Cross) | the same |
| Fill boost | The bar full (Enter / Cross) | CB3Burn + 0x38 = + 0x34 |
| Freeze damage | On: the health never goes down (it is written back at each VBlank; a repair or a new race raises it) | the same |
| Infinite boost | On: the game's own infinite boost flag (the bar stays full), set again at each VBlank (the game clears it at a crash) | CB3Burn + 0x50 ([C] CB3Burn::SetInfiniteBoost 0x0014DDE0) |

The player's car is view 0's race car, 0x0064D3B0 (gWorld 0x00522660 + 0x12AD50), while a race is on (gWorld
+0x12AD1C holds 1 to 6 race cars and the first of the pointers at +0x12AD28 is it) [C src/b3replay/capture.h,
src/native/script_mods.h]. Its visible crash damage (the bent body) is not changed by Repair.

Below those rows, **this model's tuning**: top speed in boost, acceleration, engine torque, boost kick, brakes, drag,
downforce (grip), mass, steering response, drift turn rate, wall hit penalty, from `VehiclePhysics/<your car>.cfg`.
These are tuning variables: every car of that model (a rival in the same car too) shares them, and some are read only
when a car is prepared (from the next race). Enter on one goes to its place in its tab. The model you drive is found
from the player camera's config file (`Camera/<car>.cfg`, registered for the player's car only).

## The DEVELOPER tab

| Row | What it does |
|---|---|
| SAVE AS PRESET | Writes this session's changes as a new mod in your mods folder (`tuning-<date>-<time>/`, with `mod.ini` and `tuning/preset.ini`). The mod is off until you turn it on in the launcher's Mods page. Car page edits are not part of it, nor values reset to their default. |
| LIVE OVERLAY | How many variables are registered now and how many differ from their defaults, the presets in use; F9 for the overlay. |
| RESET ALL | How many values are changed now. **Confirm** asks (`RESET ALL: SURE?`); a second Confirm within 6 seconds puts back every changed value and the car page's three ("What you changed"); any other change, or the 6 seconds, cancels. |
| CATEGORIES | The tabs above, one row each with how many values it has; CHANGED with how many are changed, the others `(n)` after their name when they hold changed values. **Confirm** (Cross / Enter) or **Right** opens one. |

In a tab, the tab has its path as a heading, a **BACK** row, **RESET THIS PAGE** when the page holds changed values
(its subsections too; more than one: a second Confirm, as RESET ALL), then the page's rows: sections (Confirm or Right
opens one; **Left** goes back up) and values: **Left / Right** change a value by one step, **Confirm** puts back its
default (the boost use rate on YOUR CAR too). A changed value is marked `*` with `(default <x>)` after it, a favourite
`+`, a section holding changed values `(n)`. CHANGED lists them as in the overlay, with its own RESET ALL row. Up /
Down held repeat as everywhere in PC SETTINGS. Search and pinning favourites are the overlay's (a keyboard and its F
key); the tab shows the same MOST USED lists.

A step is a hundredth of the variable's range when it has one, else 1 % of the value's decade (240.0 steps by 10, 2.5
by 0.1, 0 by 0.01); ints step by 1. A value never leaves the registered range (floats and ints registered with one;
14 of them have the `flags` bit 1 as well, which the port does not use).

## The F9 overlay

A box at the left of the window: the tab strip (the current tab in brackets between the ones beside it, the cursor's
place in the page at its right end), where you are (the path, with the VALUE and DEFAULT column heads), the page's rows
around a cursor (up to 15 at a time, a scroll bar at the right edge when there are more), then the focused row in full
(its whole name and value, whatever the list had to cut), its help over two lines, and the keys. A row has a gutter for
its marks (`*` changed, `+` a favourite), the label, the value and, for a changed value, its default in a column of
its own. While it is open the game sees a neutral pad (as for PC SETTINGS), so the keys never reach the race; close it to
drive on.

The box is sized to the window: the text size follows the window's height, shrinks (to no smaller than 8 pixel capitals)
when the content would be wider than the box may be (94 % of the window's width, 145 % of its height), and a label is
cut with "..." only if that is still too wide; the focused row's full name is always shown below the list. Nothing
wraps in the middle of a label. The log says how it was fitted (`[b3tune] box ... text 1.00x, 0 labels cut`).
With a mouse: the wheel moves the cursor, a click selects a row (a click on the selected row is Enter), a click on the
tab strip changes tab (its left half goes back), a right click goes back; the mouse is the overlay's only over its box.

| Keyboard | Controller | Does |
|---|---|---|
| Tab / Shift+Tab, Q / E | L1 / R1 | the next / previous tab |
| Up / Down, Page Up / Page Down | D-pad up / down | the previous / next row, a page of rows |
| Right or Enter | D-pad right, Cross | open the section |
| Left or Backspace | D-pad left, Circle | back up (Circle at the top: close) |
| Left / Right on a value (Shift: x10, Ctrl or Alt: a tenth) | D-pad left / right (R2 held: x10, L2 held: a tenth) | one step down / up |
| Enter on a value (or a car row) listed away from its place | Cross | go to its place |
| Enter on Repair, Fill boost, Freeze damage, Infinite boost | Cross | do it / turn it on or off |
| Home or R | Square | the focused value back to its default |
| Shift+R or Shift+Home | R2 held + Square | RESET SECTION (asks when more than one) |
| Ctrl+R or Ctrl+Home | Select | RESET ALL (asks) |
| Enter or Y / any other key | Cross / any other button | the question's yes / no |
| F | Triangle | pin / unpin the value (a favourite) |
| / or Ctrl+F | | search |
| F9 or Escape | Start | close |

Held, a key repeats at the system's key repeat, a D-pad button after 0.35 s, 14 a second (twice as fast after 1.5 s
for up / down). The page's rows are made when you move, and the tree when the set of registered variables changes (a
race starts or ends: native_play counts such changes, `B3TuneHooks::generation`); the box is drawn again only when its
text changes. Until 2026-11-13 that drawing cost about a second at a high-DPI window's size (a Mac's Retina screen:
each step took seconds, a player report); it is now a few milliseconds (`src/b3replay/glyphdraw.cpp`,
Canvas::strokes).

Offscreen runs (tests) can drive the overlay: `B3TUNE_OVERLAY_SCRIPT="<poll>:<action>;..."` (actions `open`, `close`,
`key:<SDL key name>` with `+shift` / `+ctrl` / `+alt`, `type:<text>`), and draw its pages over the race shots:
`B3TUNE_OVERLAY_SHOTS="tab:0,tab:3/1,tab:3/0/0,search:mass"` (`tab:<n>`, `tab:<n>/<row>[/<row>...]` that tab with
its rows opened in turn, `search:<text>`; after any of them `+pad` the controller's key lines, `+ask` the RESET ALL
question, which resets nothing), each as `live_<poll>_<alpha>_tune<k>.png` beside the shot, the overlay left as it was
before them; tools/modding/tuning_check.sh uses both.

## Edits are recorded

An edit (from the tab, the overlay or a script; a reset too) is not written at once: native_play takes it at the next VBlank (the
game's 59.94 Hz frame boundary) and writes it there to every live variable with that key. It stays for the session: a
later registration of the same key (the next race) gets it too, after the game's default and any preset.

Each edit is an **event**. A pad recording (`--record-pad`, the run logs' `.pads.txt`) gets a line

```
tune <vblank> 0x<key>[<element>] 0x<raw 32-bit value>  # <group> | <name>[i] = <value>
```

and a replay file ([remaster-features](../remaster-features.md) "Replay files") a SETS entry `tune` =
`0x<key>[<element>] 0x<raw value>`. Playing either back (`--replay-pad`, `--replay`, the launcher's Replays page)
makes the same writes on the same VBlanks, so the session plays as it was played; the console's own edits are off
while a recording plays. The raw value is the variable's 32 bits, so nothing is lost to decimal text.

**Scripted edits** (tests, comparisons): `native_play --tune "<vblank>:<name>=<value>;..."` (or `B3NATIVE_TUNE`),
names as in a preset (below), e.g.

```
native_play ... --tune "20600:Camera/Follow/ | Camera Offset[2]=-9.0"
```

At that VBlank every live variable with that name gets the value (each key is one event in the recording); a name with
no live variable logs `tuning: --tune: no registered variable is ...` and writes nothing.

**The car page's edits** are events too, with a key of their own: `0xB3CA00<field>` (01 boost, 02 health, 03 boost use
rate, 10 repair, 11 fill boost, 12 freeze damage 1 / 0, 13 infinite boost 1 / 0; element 0; `b3tune::car` in
`include/b3tune/tune.h`), in the same `tune` lines and SETS entries:

```
tune 21184 0xB3CA0011[0] 0x00000001  # car: fill boost = 240
```

native_play writes them at the VBlank on the game's thread, only while a race is on, and logs
`tuning: car: <what> on the player's car`. FREEZE DAMAGE and INFINITE BOOST, once on, are held at every VBlank until
turned off; a replay turns them on and off on the same VBlanks, so it holds them the same. They are not part of the
session's edits for later registrations, nor of SAVE AS PRESET.

## Presets

A preset is a text file in a mod, named by the mod's manifest:

```ini
# mod.ini
[mod]
id = longer-boost
version = 1
api = 1

[tuning]
preset = tuning/boost.ini
```

```ini
# tuning/boost.ini
Boost Bar | Boost Rate (Boost Units per Second) = [30.0, 30.0, 30.0, 30.0]
Score/Boost/Drift | Minimum Drift Speed (MPH) = 40.0

[../Export/ValueDB/Camera/SPRTCAR1.cfg]
Camera/Follow/ | Camera Offset = (0.0, 1.9, -7.5)

[]
0x58C54868 = 12.0
```

- **Lines**: `<group> | <name> = <value>`, or `0x<key> = <value>`; either may end in `[i]` for one element.
- **Sections**: `[<config file>]` limits the names below it to that file (one car model, one route); `[]` (or no
  section) means any file. These are the lines and sections `b3modtool vdb export` writes, so a line can be copied from
  its output.
- **Values**: a float (`2.5`, `1e-3`), an int (`3`), a vector `(x, y, z)` (w kept) or `(x, y, z, w)`, an array
  `[a, b, ...]` with all its elements, or `0x<hex>` for the raw 32 bits. The variable's type decides how a number is
  read: an int variable refuses `2.5`.
- **Comments**: a line starting with `#` or `;`, or `#` after a value. `@...` lines (VDB.XML's own, from `vdb export`)
  are skipped.

**When it applies.** Right after each registration of a variable it names (the game has just written its VDB.XML
default), and once after the defaults are loaded (for the variables registered before VDB.XML). A preset only reaches
variables that are registered: per-race ones get it as each race is prepared.

**Which value wins.** For one element of one variable: across mods, the first mod in the load order that sets it
(the launcher's Mods page, top first); within one preset, a key line beats a line under a `[file]` section, which beats
a plain name line, and of two lines of the same kind the later one. A value outside the registered range is clamped,
with a note in the log; a value of the wrong kind (or an array of the wrong length) is a note and is not used.

**What the log says.** At the start, per preset: `native_play: tuning: preset of <id> (<file>): <n> lines; this mod
changes gameplay`, and each line that cannot be read. Then each write (the first 200):

```
native_play: VBlank <n>: tuning: longer-boost registered: Boost Bar | Boost Rate (Boost Units per Second)[0] (../Export/ValueDB/Score.cfg) = 30.0 (was <the default>; longer-boost line 1)
```

**Gameplay.** A mod with a preset changes gameplay by nature: the Mods page says so on its card ("Changes gameplay: a
tuning preset"), the log says so at the start, pad recordings get a `gameplay tuning <ids>` line and replay files a
`gameplay` note. The preset file's bytes are part of the mod's hash, and so of the set hash every recording carries
([mod-format.md](mod-format.md)): a replay recorded with one preset is not played with another.

**Save as preset** (the DEVELOPER tab) writes your session's changes in this format, one line per changed element
under its config file's section, with the key in a comment. Edit it, rename the mod (`id`, `name`), share it as any
mod ([getting-started.md](getting-started.md)).

**A preset or VDB.XML?** A preset changes values as the game registers them and needs nothing of the game's files.
A changed `DATA/VDB.XML` ([b3modtool.md](b3modtool.md) "vdb") changes the defaults themselves, including values of
variables whose names are not known (the `0x<key>` lines of `vdb export`). Both are gameplay-changing. A preset is
easier to share (a few lines of text, no game data in it).

## Determinism

- **Off** (the default): nothing is wrapped; the run is the same, byte for byte, as without the console (the golden
  check, `tools/golden/compare.sh`, PASSES with it).
- **On, no edits, no presets**: the wrappers only read; the game's timeline is the same (the console's values are
  read on the game's thread at each VBlank and handed to the renderer, never written).
- **Edits** (the car page's included): written at VBlank boundaries on the game's thread and recorded with their
  VBlank: a replay is exact. The renderer never writes the game: it asks, and native_play writes at its next VBlank.
- **Presets**: written at registration, on the game's thread, in a fixed order; covered by the mod's hash.

`tools/modding/tuning_check.sh` checks all of this in the game (offscreen, the race scenario): the console on with
its RAM hashes and shots equal to the golden race run; a test preset mod (made under `build/` from your disc's values:
every follow camera twice as far behind) flagged and written, its race shot changed and its intro shot not; a scripted
edit recorded, then played back from the pad recording and from the replay file with every RAM hash, shot and edit
equal to the recording's; and a session through the overlay's own keys (a HANDLING value stepped, then the car page's
boost, FILL BOOST and FREEZE DAMAGE, an ENGINE & SPEED value stepped; then the CHANGED tab, the HANDLING value reset
from there, RESET ALL answered no, then yes) recorded and played back the same two ways, equal again.

## Limits

- Only the variables the game registers through the three AddEntry functions are seen. VDB.XML has values the scan of
  the code does not name ([vdb.md](file-formats/vdb.md) "Names"); if the game registers them at run time the console
  shows them like the others.
- A per-race variable lives only during its race: changing it in the front end does nothing until the next race,
  where the session's edit (or the preset) is applied as it registers.
- A change takes effect where the game reads the variable again. Some values are read only once (when a car or a
  route is prepared): changing them during a race may show only from the next one.
- The console cannot remove a variable or change its range; a variable whose config file VDB.XML disables is still
  written (the game itself still wrote its default), but it is not live in the console.
