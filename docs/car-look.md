# The car reflection look and its live tuner

CAR REFLECTIONS > Enhanced (remaster-features section 9) is drawn by a clearcoat layer in `src/b3gfx/shaders/car.frag`.
Every number of that layer is a runtime value (`b3gfx::CarLook`, `include/b3gfx/b3gfx.h`), so the look can be tuned while
the game runs and the final values written back into the defaults. Presentation only: nothing here reaches the game.

## What the layer does

- The paint keeps its own colour. The reflection brightens or darkens it (a scaling of the paint's colour), and a white
  sun glint is added as light only. It never tints: with `colour_keep` 1 a blue car stays blue and a black car stays a
  neutral dark grey, whatever the sky's colour.
- Enhanced is built on the game's own result: the atlas paint and the game's env pass exactly as Original draws them (a red
  car's red comes from that pass: without it the paint is magenta), and the gloss is added on top. With `strength` 0 the
  picture is Original's, pixel for pixel. `paint_darken` (0 by default) can darken the paint where it mirrors the ground.
- Facing the camera the paint keeps its colour (`fresnel_floor`, a low gloss); toward the edges of the body the sheen grows
  (`fresnel_power`).
- The environment is the track's sky map with contrast: clouds (`sky_gain`), the bright haze along a hard horizon line
  (`horizon_gain`), a dark ground below it (`ground_level`) (`paint_darken` can darken the lower body), and a hard sun
  glint (`glint_size`, `glint_gain`). `haze` adds a uniform veil; it is 0, because it lifts blacks and washes colour.
- Glass is darker (`glass_darken`) with a strong sky in it (`glass_weight`, `glass_fresnel`, `glass_gain`) and a bright
  rim (`rim`).
- Traffic is drawn at `traffic` times the racers' strength. `strength` scales the whole effect.
- Cars that see little sky (the upward share of their SH irradiance is low: tunnels, cover) show a dark surrounding, not
  a sky; night skies are dark by themselves. Where the game's env map holds the track (within 100 m of the followed car:
  tunnel walls and lights, bridges, buildings; docs/car-shading.md §9), the gloss mirrors that instead of the sky.

## The panel

PC SETTINGS > MODS > DEVELOPER MODS must be on (the panel does not exist otherwise). Then, in a race, paused or in
photo mode:

| Input | Does |
|---|---|
| F8 (the port action CAR LOOK on the controls page; rebindable) | open / close the panel (at the right of the window) |
| Up / Down, mouse wheel, click | choose a value |
| Left / Right | change it (Shift x10, Ctrl or Alt a tenth) |
| R or Home | put back its default |
| Shift+R or Ctrl+R | put back every value |
| S | SAVE: write `car_look.ini` |
| C | COPY AS CODE: print the values as a C++ initializer to the log, show them, write `car_look_values.txt` and put them on the clipboard |
| Escape | close |
| Controller: D-pad up / down, left / right (R2 held x10, L2 held a tenth), Square, Select, Cross, Triangle, Circle or Start | choose, change, reset the value, reset all, SAVE, COPY AS CODE, close |

While the panel is open the game sees a neutral pad. A changed value is drawn on the next frame on every car. A value
that differs from its default is marked `*`.

## Files

`car_look.ini` and `car_look_values.txt` are in the user data folder, beside `pc_settings.ini`:

| OS | Folder |
|---|---|
| Windows | `%APPDATA%\b3port\native\` |
| Linux | `~/.local/share/b3port/native/` (or `$XDG_DATA_HOME/b3port/native/`) |
| Mac | `~/Library/Application Support/b3port/native/` |

`car_look.ini` is read at start (a missing file: the shipped look; delete a line for that value's default). An
out-of-range value is clamped with a warning in the log. `car_look_values.txt` holds the ini lines and the code block:

```
b3gfx::CarLook{
    .strength = 0.5f,
    .fresnel_power = 4f,
    ...
}
```

Send that block back to set the shipped defaults (`CarLook` in `include/b3gfx/b3gfx.h`; `tests/test_carlook.cpp` holds the
same numbers and fails when they differ).

## The scene renderer

`b3replay <scene.b3scene> --game <install> --car-look <ini> ...` loads the same file (and `--car-reflections
off|original|enhanced` picks the mode), so screenshots can be re-rendered with the final values. `--car-look` wins over
the user's `car_look.ini`.

Original (CAR REFLECTIONS > Original, and the PS2 ORIGINAL and LOW presets) ignores all of these values: the game's own
pass is byte-identical to before.
