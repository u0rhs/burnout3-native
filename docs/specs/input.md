# Spec: host input to the DualShock 2 report

What this covers: the stick byte encoders `stick_x_byte` / `stick_y_byte` and their float-to-axis step, `gamepad_sample`
(the default gamepad bindings, the dead zone and the trigger amounts; the defaults of `GamepadTuning`, 0.24 and 0.25, are
user-visible settings) and the pressure-byte table (`pressure_byte`), all in `src/native/input_keys.cpp` behind
`src/native/input_keys.h`. Sources: the public PS2SDK `libpad.h` (`padButtonStatus`, the `PAD_*` button masks), the
SDK's `libpad2.h` button indices, and SDL3's gamepad documentation (standard layout, axis ranges).

Around it (not specified here): `KeyPad` (bindings with menu / drive contexts, the steering and D-pad pressure ramps,
`sample`, `compose`), `default_key_map` (the default layout), `merge_samples`, `BoostLatch`, `PadSeats`,
`neutral_sample`, `ds2_name`, the SDL adapter (`src/native/input_keys_sdl.h`, `src/b3replay/nativepad.{h,cpp}`) and
the neutral report of `src/native/platform_dbc.cpp`. KeyPad's `sample` calls the functions specified here.

## 1. The report (32 bytes) [PS2SDK libpad.h padButtonStatus; SDK libpad2.h]

- Bytes 0–1: the 16 digital buttons, **active low** (0 = pressed). Bit i of the 16-bit little-endian word is button
  index i: Select 0, L3 1, R3 2, Start 3, Up 4, Right 5, Down 6, Left 7, L2 8, R2 9, L1 10, R1 11, Triangle 12,
  Circle 13, Cross 14, Square 15 (`Ds2` enum values 0–15; PS2SDK's PAD_SELECT 0x0001 ... PAD_SQUARE 0x8000).
- Bytes 2–5: right stick X, right stick Y, left stick X, left stick Y (0 = left / up, 255 = right / down).
- Bytes 6–17: the pressures 0–255 of Right, Left, Up, Down, Triangle, Circle, Cross, Square, L1, R1, L2, R2, in that
  order (padButtonStatus's right_p ... r2_p). Select, L3, R3 and Start have no pressure byte.
- Neutral report (unchanged, `neutral_sample`, equal to platform_dbc's): bytes 0–1 0xFF, sticks 0x80 0x7F 0x80 0x7F,
  bytes 18–21 0x80, all else 0.

## 2. Stick encoding (`stick_x_byte(float v)`, `stick_y_byte(float v)`)

v ∈ [−1, 1] (clamped), −1 = left / up. First the SDL axis value w: w = v · 32768 for v < 0 and v · 32767 otherwise,
the product formed in single precision (float × float, rounded to a float), then rounded to the nearest integer with
halves away from zero (the exact product differs at 127 of 2^32 inputs). Then:
- X byte: w ≥ 0 → 128 + the nearest integer to 127 · w / 32767 (exact halves cannot occur); w < 0 → 128 − (|w| / 256
  rounded to nearest, halves up).
- Y byte: w ≥ 0 → 127 + the nearest integer to 128 · w / 32767 (no exact halves); w < 0 → 127 − (127 · |w| / 32768
  rounded to nearest, halves up).
Checks: X(−1) = 0, X(0) = 128, X(1) = 255; Y(−1) = 0, Y(0) = 127, Y(1) = 255. These bytes reach the game (the
scripted keys of every race_check tour steer through KeyPad), so every value must match exactly: the checks
cover all floats in [−1, 1] that the ramp can produce and an exhaustive sweep of w (§5).

## 3. Gamepad to report (`gamepad_sample(const GamepadState&, const GamepadTuning&)`)

Inputs as SDL gives them: sticks −32768..32767 (Y: down positive), triggers 0..32767, buttons in SDL's standard
(position-named) layout. Tuning: dead zone z = clamp(stick_dead_zone, 0, 0.95), threshold θ = clamp(
trigger_threshold, 0.01, 1).
- Buttons: South → Cross, East → Circle, West → Square, North → Triangle, left / right shoulder → L1 / R1, Start →
  Start, Back → Select, D-pad → Up / Down / Left / Right, left stick click → L2 (the music track skip, 0x12F460),
  right stick click → R2 (the Crashbreaker, 0x12F610). No gamepad binding for L3, R3 or the right stick's axes. (The
  triggers are the pedals below, so the stick clicks carry the two driving controls a pad could not reach before
  padr2, 2026-11-13.)
  A bound button sets its digital bit when held, and its pressure byte (if it has one) to 255 when held, 0 when not:
  L2 / R2 from the stick clicks too (bytes 16 / 17), as the bindings path gives every digital input on a button with
  a pressure byte (`input_bindings.h` `gamepad_sample(g, default_player(p).pad, t)` equals this function byte for byte: the 1.x
  layout, which 1.2.1 keeps as the Custom layout's starting bindings; the default CONTROLLER LAYOUT is Auto, the
  console layouts of docs/pc-settings.md §7.25).
- Triggers as analog pedals: t = clamp(max(raw, 0) / 32767, 0, 1). Cross's pressure = round(255 · max(South ? 1 : 0,
  t_right)), Cross pressed when South is held or t_right ≥ θ; Square likewise with West and the left trigger. (A
  trigger below the threshold still reports its pressure: the game's accelerator / brake read the pressure.)
- Left stick, per axis: n = raw / 32768 for raw < 0, raw / 32767 otherwise; the positive half amount is
  clamp((n − z) / (1 − z), 0, 1), the negative half amount the same with −n; the axis value is positive minus
  negative (a dead zone around the centre, the rest rescaled to full range). X and Y bytes via §2.
- Pressure rounding: round to nearest, halves away from zero (255 · 0.5000153 → 128).
- With no input the result equals `neutral_sample()`.

## 4. Performance constraints
Called once per VBlank per pad (and per launcher frame): no allocation, no I/O. Header signatures unchanged.

## 5. Acceptance checks
1. `make test`: `test_input_keys` (incl. its gamepad rows: half trigger → pressure 128, 6000 → 47 not pressed, dead
   zone 7000 → 0x80, 20000 → the §2 value of 0.4874), `test_input_keys_sdl`, `test_menu_input`, `test_photomode`,
   `test_launcher`. New rows for each rule above with hand-computed bytes (and a few lines of independent Python for
   the formulas of §2, run with `python3 -I`).
2. Sweeps: `stick_x_byte` / `stick_y_byte` on every float the ramp can produce (the ramp's steps for 1/60 s and the
   0.12 / 0.06 s times, both directions, plus 10^7 random floats in [−1, 1] and all ±k/32768) against the §2 formulas;
   `gamepad_sample` for every stick value −32768..32767 on each axis (others neutral) × dead zones {0, 0.24, 0.5},
   every trigger value × thresholds {0.01, 0.25, 1}, and 10^6 random full states, against §3.
3. race_check `STRICT=1` race and every front-end tour PASS with shots equal to the golden recording (keyboard steering
   and menu navigation through KeyPad), `SCENARIO=multiplayer` (the second pad, `--keys2`).
