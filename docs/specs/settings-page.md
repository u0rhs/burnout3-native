# Spec: the PC SETTINGS page's menu model (and the camera FOV correction)

What this covers: the menu model `b3settings::Menu` in `include/b3settings/page.h` / `src/b3settings/page.cpp`
(open, update, the hold repeat, focus / scroll / tab memory, pointer and drag handling, `items()` layout, colours and
animation) and `fallback_measure`; the UI item vocabulary in page.h (`ItemKind`, the flag bits, `kItalicSlant`,
`kItalicWidth`) that `src/b3replay/pcpage.{h,cpp}` draws; the FOV correction's eye dolly in `src/b3replay/view.cpp`
`View::apply_fov`; and `src/b3replay/fpspanel.cpp`'s FPS panel (§5).

Around it (not specified here): `Settings`, presets, the ini, HSET (`include/b3settings/settings.h`,
`src/b3settings/settings.cpp`), `build_content` / `apply_edit` and every row id, label, value text and description
(`docs/pc-settings.md` §3 onwards), `Row`, `Tab`, `Content`, `Env`, `Edit`, `Input`, `Frame`, `Item`'s fields,
`fov_tangent_scale` (the standard tan(fov/2) / tan(45°) ratio).

**Look**: the page takes its visual cues from the game's own option pages (the `ddsettings` front-end tour's shots):
its italic menu font, its blue panels, the L1 / R1 tab prompts.

## 1. Interface (page.h; used by pcpage.cpp and tests/test_settings.cpp)

- `void Menu::open(std::size_t tab = 0)`; `std::size_t tab() const`; `std::size_t focus() const` (index of the focused
  row in the current tab's row list).
- `Frame Menu::update(const Content&, const Input&)`: one poll; returns the edits the user made (`Edit{id, value,
  direction, action}`) and `close`.
- `std::vector<Item> Menu::items(const std::string& fonts, const std::string& art, const Measure& measure) const`: the
  page as UI items in the game's 640 × 448 menu picture. `Measure(text, letter_height, flags)` gives a text's drawn
  width; when empty, `fallback_measure` is used (a per-character width estimate; any reasonable estimate).
- `Item{kind, flags, x, y, w, h, colour, text}`: `colour` RGBA with R in the low byte; `kind` one of image, text,
  rect, font directory. The first item is a font-directory item whose text is `fonts`. Image items name a file
  relative to `art` (`art + "/" + name`, or the bare name when `art` is empty). pcpage.cpp draws rects as quads,
  texts with the game's captured fonts (it needs: a size field (small / large / the slanted menu face), right
  alignment, the heading face, "no outline", and the menu face's slant and widening factors), and images by name
  (today: value arrows as triangles, the L1 / R1 tab prompts as the device's prompt text). pcpage.cpp interprets
  all of the vocabulary.

## 2. Input model (`update`)

Per poll the caller passes held states (`up down left right confirm back previous_tab next_tab`), an optional
pointer position in picture pixels with `pointer_moved`, `click` (pressed this poll), `click_held`, a wheel step count
and `seconds` since the previous poll (the caller's deterministic clock: the model never reads real time).

1. **Content refresh**: the content is replaced every poll (rows are rebuilt from the settings). The focused row is
   kept by its **row id** across refreshes; if it disappeared or is a section header, focus goes to the first
   selectable row. A tab index beyond the tab count resets to tab 0.
2. **Edges**: confirm, back, previous-tab and next-tab act on a press edge only. The press that opened the page
   (`open` was called while it is held) must be released before it counts.
3. **Hold repeat** for up / down / left / right: a press acts at once; held, it repeats after an initial delay and then
   at a fixed interval, measured with `seconds` (values in 0.3–0.5 s and 50–100 ms; at most one step per poll).
   Left and right held together count as neither. Up / down held go faster in a long list (devnav1: the interval halved
   from 1.5 s held, ~60 a second from 3 s; left / right never). **Sections**: previous / next section (PageUp /
   PageDown, a pad's L2 / R2) move to the first selectable row of the next section, or of the focused row's section
   and then the one before; in a tab without sections by a window of rows; held, they repeat as up / down.
4. **Popup**: while `Content::popup` is non-empty nothing acts (edges and repeat states still track the inputs, so a
   press held across the popup does not fire when it closes); the popup is drawn over the page.
5. Order in one poll: back → `close = true`, nothing else. Else previous / next tab → move one tab (no wrap), nothing
   else this poll. Else up / down move the focus, left / right change the focused row, confirm activates it, then the
   pointer is handled.
6. **Focus**: up / down go to the previous / next *selectable* row (section headers are skipped); no wrap at the ends.
7. **Change** (left = −1, right = +1) on the focused row: toggle, choice, slider → the value one step down / up, no
   wrap, nothing at the ends; an action row → `Edit{id, 0, direction, action = true}`; a disabled row → nothing.
8. **Activate** (confirm, direction 0): an action row → an action edit; a toggle → flips it; others nothing; disabled →
   nothing.
9. An edit carries the row id and the new value; it is produced only when the value actually changes and is within
   range (toggle 2 values, choice `values.size()`, slider `levels`). The model never applies edits itself.
10. **Tabs**: each tab remembers its focused row while the page is open (forgotten by `open`); switching restores it
    (or the first selectable row), scrolls to show it, stops a slider drag and restarts the row animation.
11. **Scrolling**: when a tab has more rows than fit, a window of rows is shown; the focused row is always inside it,
    and when the row above the focused one is its section header, the header is shown too if possible. The window
    never runs past the last row. Scroll indicators (up / down) show when rows are hidden above / below.
12. **Pointer**:
    - a click on a tab label selects that tab;
    - when rows are hidden, a click on a scroll indicator scrolls one row; the wheel scrolls by its steps (clamped);
    - hovering (pointer moved) or clicking over a selectable row focuses it (section rows ignore the pointer);
    - a click on a row's control: toggle → flips; choice → its left / right arrow steps it, a click on its value
      bar (if you draw one) sets the value under the pointer; slider → its arrows step it, a click on its bar sets
      the nearest level (rounded) and starts a drag that keeps setting it while `click_held` stays true and the
      focused row is still that slider; action → activates. Disabled rows ignore clicks.
    - no pointer this poll ends a drag.

## 3. Drawing requirements (`items`)
- Everything inside the 640 × 448 picture (a 1-pixel tolerance), since the page is drawn over the game's 4:3 menu
  box. A backdrop that hides the main menu behind it; a title "SETTINGS"; the tab labels with L1 / R1 prompts; the
  visible rows with their label, a control per kind (toggle, choice with its value text and position, slider with
  its level text, action button), the focused row clearly marked, disabled rows dimmed; the focused row's
  description; a footer saying Back closes and saves; the popup when set.
- Texts are clipped to their box by the renderer; keep texts ≤ 1,024 bytes.
- Any opening / tab-switch animation must be a pure function of the time passed through `seconds` (replays and
  screenshots are deterministic) and must not move items outside the picture once settled.
- Coordinates rounded to whole pixels for rects and images.

## 4. FOV correction (`View::apply_fov`, view.cpp)
Drawn only; the game's camera stays retail. With `fov_scale` = `fov_tangent_scale(Settings::fov)`:
- `fov_scale == 1` (the default: the game's 90°) or an invalid camera → nothing changes (so every golden run is
  unaffected).
- Otherwise the view's half-width and half-height tangents are multiplied by `fov_scale`.
- With `fov_correction` on and `fov_scale > 0`, the eye is moved along the view direction so that the car being
  chased keeps its on-screen size: an object at distance d keeps its size when the eye moves forward by
  d · (1 − 1/fov_scale) (similar triangles; negative = backwards, for a field of view narrower than the game's).
  The car chased is a race car in front of the eye, near the view axis, within a chase-camera distance; d is
  measured to it (e.g. to the car's rear), and a forward move is limited so the eye never reaches the car (a
  clearance is kept). Views with no such car in front (in-car,
  bumper, replays' trackside cameras): no move.

## 5. FPS counter (fpspanel.cpp)
Functional contract (the full design is docs/native-renderer.md §28.1):
- Rows (INTERFACE, first): FPS COUNTER (Off / Minimal / Standard / Detailed; ini `fps_view` 0-3, row id 255) and FPS COUNTER
  POSITION (top left / top right / bottom left / bottom right; ini `fps_corner` 0-3, row id 256). An ini written before the
  views holds `fps_counter = true | false`: true reads as Standard, false as Off; `fps_view`, when present, wins.
  `--fps-counter` shows at least Standard.
- A shortcut (port action FPS COUNTER VIEW, controls.ini `fps_view`, F3 by default, remappable) cycles Minimal, Standard,
  Detailed, Off for the session.
- Content per view: Minimal the frame rate on a small plate; Standard also the 1 % low (no graph; both at most about 8 % of
  the window's width); Detailed the big plate: also a frame-time graph of the last 8 s with a target
  line (the frame rate limit's frame time, else 60 Hz's), the average, current and worst frame time, the
  latency line when `--latency` measured one, the render size / MSAA / preset and the GPU with its backend.
- Placement: the chosen corner, a margin of 4.5 % of the window's height, scaled by the window height and HUD SIZE, at
  every aspect; below / above the race HUD's plates in a race; clear of notices; in the top left while photo mode's display is up; never in a screenshot.
- Presentation only; off by default.

## 6. Performance constraints
The page runs once per frame only while open; no constraint beyond "no per-frame file I/O or allocation spikes
visible in the frame time". `apply_fov` runs per frame: O(vehicles), no allocation.

## 7. Acceptance checks
1. `make test`: `test_settings` all pass. The menu tests (`menu_edits_tabs_and_close`, `menu_items_inside_the_picture`,
   `menu_pointer`) find positions through `items()` (e.g. click the centre of the third tab's label item), not fixed
   coordinates. Tests for every rule of §2 (edges with the opening press, repeat timing, popup suppression, no wrap,
   disabled rows, focus kept by id across a content change, tab memory, scroll window with section header, slider drag, wheel).
2. Visual check in a window (the page is not covered by the golden runs): the page opened from the main menu and
   from the race's pause menu on Windows, with keyboard, gamepad and mouse; every tab; a slider drag; the save
   export / import actions; the HD-pack and music folder actions.
3. Golden runs unaffected: race_check `STRICT=1` race and the `ddsettings` / `carselect` tours PASS with shots equal to
   the baseline (the page is closed in them; FOV default).
4. FOV: with FOV 120 and with FOV 60, correction on, a chase view keeps the car about the same size as at 90
   (screenshot triple from the same replay poll); correction off: only the field of view changes.
5. Windows `RENDER=1 tools/native/build_win.sh` links `b3render.dll` with the page.
