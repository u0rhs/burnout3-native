# Tutorial 06: a Lua script

In this tutorial you make a **script mod**: a speedometer box over the race with the player's speed and the race time,
a row on PC SETTINGS that moves it, and a log line at each lap. A script mod is a folder with a `mod.ini` and Lua
files: no compiler, no build step, the same files on Windows, Linux and macOS. About fifteen minutes; you need a text
editor. Tutorial [05](05-an-overlay.md) does the same kind of drawing from C; this one needs none of it.

The finished mod is [examples/mods/lua-speedo](../../../examples/mods/lua-speedo). Everything the script can call:
[the Lua reference](../lua-reference.md).

## 1. The mod folder

```
lua-speedo/
    mod.ini
    scripts/
        main.lua        the script the game runs
        format.lua      a helper main.lua requires
    README.md
```

`mod.ini`:

```ini
[mod]
id = lua-speedo
name = Lua speedometer
version = 1.0
api = 3

[script]
main = scripts/main.lua
gameplay = no
```

- `api = 3` is the mod API with scripts.
- `main` is the script the game runs. It has to be in a folder of the mod (`scripts/`): that folder is the script
  folder, where `require` finds the other files, and all of its files are part of the mod's hash (a replay made with
  another version of the script says so).
- `gameplay = no` says the script only reads and draws. The game holds it to that: a write to the game's memory, a
  call of a game function or a replaced function would be refused. A script that changes the game says
  `gameplay = yes`, and the launcher marks it as changing gameplay.

## 2. The script

`scripts/format.lua` is a plain Lua module:

```lua
local M = {}

-- seconds -> "M:SS.hh" ("-:--.--" for the game's "no time", FLT_MAX after being out of a race)
function M.time(t)
    if t == nil or t >= 3.0e38 or t < 0 then return "-:--.--" end
    local hundredths = math.floor(t * 100 + 0.5)
    return string.format("%d:%02d.%02d", hundredths // 6000, (hundredths // 100) % 60, hundredths % 100)
end

return M
```

`scripts/main.lua`:

```lua
local format = require("format")   -- scripts/format.lua

b3.settings.add{
    key = "place", label = "Speedometer box", kind = "choice",
    values = { "Top left", "Bottom left" }, default = "Top left",
    description = "Where the box with the speed and the race time is drawn.",
}

local laps = {}

b3.on("race_start", function(e)
    laps = {}
    b3.log("race start at VBlank " .. e.vblank)
end)

b3.on("lap", function(e)
    laps[#laps + 1] = format.time(e.time)
    b3.log("lap " .. e.lap .. ": " .. format.time(e.time))
end)

b3.on_frame(function()
    local player, score = b3.view.player(), b3.view.score()
    if not player or not score then return end   -- not in a race: nothing to draw
    local y = b3.settings.get("place") == "Top left" and 72 or 330
    b3.draw.quad(16, y, 172, 52, b3.rgba(0, 0, 0, 170))                 -- the box
    b3.draw.quad(16, y, 4, 52, b3.rgba(255, 150, 0))                     -- an orange edge
    b3.draw.text(28, y + 8, 14, b3.rgba(255, 220, 60), string.format("SPEED %3d", player.speed_shown))
    b3.draw.text(28, y + 30, 12, b3.rgba(255, 255, 255), "TIME " .. format.time(score.time))
end)

b3.on_shutdown(function()
    if #laps > 0 then b3.log("laps this run: " .. table.concat(laps, ", ")) end
end)
```

How it works:

- The file runs once, before the game's first instruction: its **main chunk**. That is where a script adds settings rows
  and says which game events it wants (both are fixed when the game starts). `b3.settings.add` puts a row on
  PC SETTINGS > MODS and on the launcher's Mods page; `b3.settings.get("place")` gives its value now, the name of the
  choice.
- `b3.on("race_start", ...)` and `b3.on("lap", ...)` are **game events**: the port calls the function when the player's
  race starts (its score's state turns to racing) and when the player crosses a lap line, with a table of what happened
  (`e.vblank`, `e.lap`, `e.time`).
- `b3.on_frame` runs at every game frame (each time the game reads the pad): draw there, every time, as in tutorial 05.
- `b3.view.player()` and `b3.view.score()` read the game's memory now and return tables with named fields: here
  `player.speed_shown` (the number the game's speedometer shows, in its units) and `score.time` (the race time in
  seconds). Outside a race they return `nil`, and the script draws nothing.
- `b3.draw.quad` and `b3.draw.text` work in the game's 640 x 448 screen; `b3.rgba(r, g, b, a)` makes a colour. The text
  font has capital letters, digits, space and `+ - / . : ;` (the `:` and `.` of the time are in it).
- `b3.log` writes `mod lua-speedo: ...` into the game's log.

There is no `io`, `os` or `debug` library, no clock but `b3.vblank()`, and `math.random` starts from a seed made from the
mod's id, so the script does the same thing in every run of the same race.

## 3. See it

Copy the `lua-speedo` folder into the mods folder (`<user data folder>/mods/`; the launcher's Mods page has "Open mods
folder"), press Look again and turn it on, then start the game. Script mods are not asked about like code mods: they
can only use what the `b3` table offers. The log says:

```
native_play: mods: lua-speedo: a script (scripts/main.lua; gameplay = no: reads and draws only)
native_play: mods: lua-speedo: script loaded (0 replaced, 0 wrapped, 1 settings, 2 callbacks)
```

In a race the box is at the top left, under the game's POS panel: `SPEED  87` (the same number as the game's own
speedometer at the bottom right, `87 mph`) and `TIME 0:08.98`, changing as you drive. PC SETTINGS > MODS has the row **SPEEDOMETER BOX**; choose Bottom left and the box moves at the next VBlank.

Without a window (the port's checks), run the race check with the mod and look at its screenshots:

```
$ MODS=lua-speedo MODS_DIR=examples/mods STRICT=1 tools/native/race_check.sh build/native_play build/rc_speedo
...
race_check: PASS race US/C2_V2 reached and driven to VBlank 23400; ...
$ grep "lua-speedo" build/rc_speedo/native_play.log
native_play: mods: lua-speedo 1.0 ("Lua speedometer") from examples/mods/lua-speedo: 0 files, mod hash 1b4906b5210f8646
native_play: mods: lua-speedo: a script (scripts/main.lua; gameplay = no: reads and draws only)
native_play: mods: active (--mods; the first wins): lua-speedo 1.0 (0 files, script); set hash f9c88d65f19f84f4
native_play: mods: lua-speedo: script loaded (0 replaced, 0 wrapped, 1 settings, 2 callbacks)
mod lua-speedo: race start at VBlank 21070
```

The shots are `build/rc_speedo/shots/live_<poll>_0.png` (polls 10700 and 11500 are in the race: at 10700 the
countdown, `TIME 0:00.00`; at 11500 `SPEED  87`, `TIME 0:08.98`). The check's race ends before the first lap line, so its log
has no lap; a longer race logs one line per lap (`mod lua-speedo: lap 1: <its time>`). The script only draws,
so the game runs as without it, and a replay of the run plays back equal:

```
$ MODS=lua-speedo MODS_DIR=examples/mods SCENARIOS=race tools/native/replay_check.sh build/native_play build/replay_speedo
replay_check: race native_play: replay: 39 of 39 checkpoints matched; it played as recorded
replay_check: race compare: race shots: PASS 2 equal
replay_check: race compare: race ram: PASS 234 grid VBlanks x 32 MB equal
...
replay_check: PASS race: each replay played back with every compared log equal to its recording's (in .../build/replay_speedo)
```

The settings row can be changed from a check too, at a VBlank of your choice (recorded like a change on the page):

```
$ B3NATIVE_MOD_SET="21300:lua-speedo.place=Bottom left" MODS=lua-speedo MODS_DIR=examples/mods STRICT=1 tools/native/race_check.sh build/native_play build/rc_speedo_set
...
native_play: VBlank 21300: mods: setting lua-speedo.place = Bottom left
```

and the box in the shot at poll 11500 is at the bottom left, above the boost bar.

## 4. Change it while the game runs (hot reload)

Turn on **Developer mods** on the launcher's Mods page (the same switch as PC SETTINGS > INTERFACE > DEVELOPER MODS;
or start native_play with `--developer-mods`). The game then
looks at the script folder twice a second; save a change to `main.lua` (make the box red: `b3.rgba(120, 0, 0, 170)`) and
the script is loaded again within half a second (the port checked this with a race run that changed the box's colour
just after the start: the next shot has the red box):

```
mod lua-speedo: its script changed: loaded again at VBlank 21091 (hot reload, Developer mods: from here this run does not play back as recorded)
```

Its state starts again (the `laps` list is empty); functions given to `b3.on_load(fn)` run with `true`, so a script can
rebuild what it needs. A mistake in the new file stops the script with the error in the log; the next save that works
brings it back. Without Developer mods a script is read once, at the start.

## 5. When a script goes wrong

A script that runs too long in one VBlank (a loop that never ends), uses too much memory, or raises an error the script
does not catch is **stopped**; the game goes on without it:

```
mod lua-runaway: starting a loop that does not end
mod lua-runaway: error: the script was stopped at VBlank 21800: it ran past 1000000 Lua instructions in one VBlank (its budget: a loop that does not end?); the game goes on without it
mod lua-runaway: error:   the script was stopped (its instruction budget, or an error before)
mod lua-runaway: error:   scripts/main.lua:7: in function <scripts/main.lua:2>
```

(That is a test script whose `on_vblank` starts `while true do ... end` in the race; the race goes on to its end and the
check passes.) Its drawing ends, and its settings row stays. The limits are in `mod.ini` (`budget`, `memory`;
[the reference](../lua-reference.md#limits-the-budget-and-the-memory-cap)).

## 6. Make it yours

- **More of the race**: `b3.view.score()` has `lap`, `laps`, `lap_time`, `best_lap`, `lap_times`, `takedowns`;
  `b3.view.player()` has `x`, `y`, `z` and `speed` (metres per second); `b3.view.race()` says whether a race is on,
  split screen, Time Attack.
- **More events**: `b3.on("crash", fn)`, `b3.on("takedown", fn)`, `b3.on("finish", fn)`; `e.player` says whether it was
  the player.
- **Your own pictures**: `b3.draw.texture(w, h, pixels)` once in the main chunk, then `b3.draw.quad(..., id)`.
- **Your own files**: `b3.data.read("data/table.txt")` reads a file of the mod; `b3.data.read("disc:DATA/...")` a disc
  file.
- **Change the game**: say `gameplay = yes` and use `b3.mem.write`, `b3.call` and `b3.hook` ([the
  reference](../lua-reference.md#calls-and-hooks)); times and replays made with such a mod are kept apart.
