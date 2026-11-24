# Known issues

Problems we know about in the current build. If you hit one anyway, a bug report still helps: press **F10** in the
game (or **Bug report...** on the launcher's Play page) right after it happens and attach the zip to a
[GitHub issue](https://github.com/u0rhs/burnout3-native/issues/new/choose). Your user name is taken out of the folder paths in the zip, and it holds no
game data.

## Gameplay and stability

- Very rare crashes or hangs are still possible. A bug report made right after one is the most useful thing you can
  send: it includes a recording of your controls that replays the session exactly.
- Race with 6 or 7 rivals (SELECT OPTIONS' RIVALS above 5): the 6th and 7th rival drive the same cars as two of the
  others, in another paint, and start behind the grid (or ahead of it where a wall is behind). Single Event Race only.

## Controls

- Every key and button of the game and of the port's shortcuts can be changed (PLAYING.md, section 5). What stays
  fixed: Escape cancels a rebind, so no control can be moved to it; Alt+Enter also switches fullscreen; the mouse in
  photo mode looks and zooms; the launcher's menus use the arrow keys, Enter, Escape and Tab (and the D-pad, A and B),
  so that the page that changes the controls can always be reached; and the tuning overlay's own navigation keys (a
  tool for mod makers) are fixed, only the key that opens it can be changed.
- The photo mode help box lists the first key or button you set for each control, so it can run wider than the
  default text.

- Windows: TRIGGER RUMBLE needs the game started again after turning it on (it changes how the game reads
  controllers). If a controller behaves differently with it on (a pad not seen, one on the Xbox Wireless Adapter
  switching off, the Guide button, two identical pads), turn it off and tell us.

## Presentation

- The orbit camera (the right stick in a race) stops short of the walls, pillars and signs it can see, but not of the
  game's invisible collision walls: beside a low barrier it can stand behind the barrier and look over it. Seen from the
  side, the game's tyre smoke and dust look thin: they are flat sprites turned to the game's own camera (photo mode
  shows the same).
- DRIVER LABELS show the names the game gives its rivals; on the US disc they are DRIVER1 to DRIVER5.
- Night and rain (SETTINGS > EXTRAS > EXPERIMENTAL) are experimental: still being finished. They change only the
  picture and sound, so their races count as usual. Tell us what you think, and what looks wrong.
- The Signature Takedown's flash (issue #17) is still being looked into.
- Cars far ahead fade in over the last stretch of the game's draw distance instead of popping in. Tell us if any
  still pop in.

## Performance

- On slower PCs a race can fall behind real time, and the sound then stutters. FRAME RATE LIMIT: Auto (the default)
  holds the frame rate to 60 on a processor with 4 threads or fewer, and on any PC the frame rate is held to 60 while
  the game is behind. If it still falls behind, lower the settings (the **Low** preset, no MSAA) or start in safe mode from the launcher.

## Platforms

- **Linux**: tested on one desktop so far; reports from other distributions and the Steam Deck are very welcome.
- **macOS (Apple Silicon)**: the app is not signed by a registered Apple developer, so the first start needs **Open
  Anyway** in System Settings > Privacy & Security (the README's Get started section shows how).

## Not in the game yet

- Online play (EA's servers are gone; online is coming in a future release). Picking ONLINE shows the message
  "Online play is coming soon."
- More than three profiles per save, new car slots and events: see the feature list in the
  [README](../README.md#features).
