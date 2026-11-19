# Burnout 3 native 1.2.0 (release notes)

## New

- **CHD disc images.** The launcher takes a `.chd` of your disc as well as an `.iso` or raw `.bin`, read in place
  (nothing is extracted). Both kinds work: CD-style (`chdman createcd`) and DVD-style (`chdman createdvd`). Loading is
  as quick as from an ISO.
- **Your own songs in EA TRAX.** With a music folder chosen (launcher Music page, or PC SETTINGS) and PLAYLIST set to
  Mixed or Custom only, your songs appear in the game's own EA TRAX page after the 44 originals, with their title,
  artist and album, and each can be set to ALL / RACE ONLY / MENU ONLY / OFF like the game's songs. Your choices are
  kept beside your settings (`music_modes.ini`); the save itself only ever holds the game's songs, so it stays valid
  on a PS2 or in PCSX2.
- **HD textures on a key.** With an HD texture pack loaded, **F4** switches between the HD textures and the originals
  at once, for comparing (rebindable on the CONTROLS page). Your HD TEXTURES setting stays as you chose it.
- **Reflections of the world.** As on a PS2, cars now reflect the track around them, so in a tunnel they mirror the
  tunnel and its lights instead of the bright sky. Both CAR REFLECTIONS looks (Original and Enhanced); the LOW preset
  keeps the lighter sky-only reflections.

## Fixed

- **Engine sound on headphones.** The engine no longer splits across the stereo field (bass on one side, mids and
  highs on the other). It now goes through the game's own engine mix as on a PS2: centred, with the PS2's engine
  distortion, and the race mix at the PS2's level (1.1.0's race was about 2 dB too loud).
- **Crash-mode pickups on Windows** (boost, cash multipliers and the rest) were sometimes not collected when driven
  through.
- **Cars wobbling apart when hit.** When a car is slammed, its panels and wheels now move with the body, instead of
  staying behind.
- **A crash with six-wheeled trucks.** When a six-wheeled vehicle (the heavyweight trucks in Crash mode, or traffic
  trucks thrown into a crash) bottomed out its rear suspension, the game could stop on a Mac, and could quietly
  misbehave elsewhere. Also in 1.1.0.
- **Windows: "b3render.dll did not load (error 1114)"** when another program's DLL (an overlay, a mod loader, a stray
  `version.dll` in the folder) got in the way: the renderer now takes Windows' own DLLs from Windows only. If it still
  cannot load, the message says why and the run log lists what was involved.
- **Windows: user names with accents or non-Latin letters.** A game folder under such a name (`C:\Users\Jürgen\...`)
  could stop the game at start.

## Notes

- Replays recorded with a music folder in 1.1.0 may not play back the same (the song choice changed); other replays
  are unaffected.
- Saves and settings from 1.0.0 and 1.1.0 carry over. In 1.1.0, the launcher's **Install update** installs this
  release.
