# Known issues

Problems we know about in the current build. If you hit one anyway, a bug report still helps: press **F10** in the
game (or **Bug report...** on the launcher's Play page) right after it happens and attach the zip to a
[GitHub issue](https://github.com/u0rhs/burnout3-native/issues/new/choose). Your user name is taken out of the folder paths in the zip, and it holds no
game data.

## Gameplay and stability

- Very rare crashes or hangs are still possible. A bug report made right after one is the most useful thing you can
  send: it includes a recording of your controls that replays the session exactly.

## Controls

- Every key and button of the game and of the port's shortcuts can be changed (PLAYING.md, section 5). What stays
  fixed: Escape cancels a rebind, so no control can be moved to it; Alt+Enter also switches fullscreen; the mouse in
  photo mode looks and zooms; the launcher's menus use the arrow keys, Enter, Escape and Tab (and the D-pad, A and B),
  so that the page that changes the controls can always be reached; and the tuning overlay's own navigation keys (a
  tool for mod makers) are fixed, only the key that opens it can be changed.
- The photo mode help box lists the first key or button you set for each control, so it can run wider than the
  default text.

## Presentation

- Cars far ahead fade in over the last stretch of the game's draw distance instead of popping in. Tell us if any
  still pop in.

## Performance

- On slower PCs a race can fall behind real time, and the sound then stutters. Lower the settings (the **Low** preset,
  no MSAA) or start in safe mode from the launcher.

## Platforms

- **Linux**: tested on one desktop so far; reports from other distributions and the Steam Deck are very welcome.
- **macOS (Apple Silicon)**: the app is not signed by a registered Apple developer, so the first start needs **Open
  Anyway** in System Settings > Privacy & Security (the README's Get started section shows how).

## Not in the game yet

- Online play (EA's servers are gone; online is coming in a future release). Picking ONLINE shows the message
  "Online play is coming soon."
- More than three profiles per save, new car slots and events: see the feature list in the
  [README](../README.md#features).
