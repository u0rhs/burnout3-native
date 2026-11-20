# Burnout 3 native 1.3.0 (release notes)

Car shadows, your own car paint, voices in your language, render scale up to 200%, a tidier SETTINGS page and
launcher, safer saving, and a fix for the split-screen crash.

## New

- **Sun shadows.** SUN SHADOWS (GRAPHICS > LIGHTING) gives the cars a shaped shadow from the sun, on top of the game's
  own soft shadow. Off by default.
- **Car paint.** CAR PAINT: Custom (SETTINGS > CAR) draws your car in your own colour, with a Gloss, Metallic or
  Pearl FINISH, and DECALS Off removes its logos and stripes. PLAYER 2 CAR PAINT does the same in split screen, and
  the launcher's Showroom shows it. The picture only: the game, replays and saves keep the paint you picked.
- **Look around your car.** On SELECT CAR and YOUR GARAGE the right stick, or a mouse drag, turns the view around the
  car (CAR > GARAGE > GARAGE CAMERA).
- **Voices in your language.** With LANGUAGE set to French, German, Italian or Spanish, the race training is narrated
  in that language. The Crash FM DJ can speak it too: import it from your own European disc with DJ VOICE, beside
  IMPORT LANGUAGES (GAME > LANGUAGE AND UNITS). TRAINING VIDEOS imports the training videos' pictures in that language. VOICES: English
  keeps the English voices.
- **Motion blur in photo mode.** MOTION (J, or a click of the right stick) steps Off / Tracking / Panning / Static;
  K and L, or the right stick held with D-pad left / right, set the SHUTTER from 1/500 to 1/15 s. If you had J, K or L
  bound to something else, check CONTROLS > BINDINGS.
- **Sharper picture on a fast graphics card.** RENDER SCALE (GRAPHICS > IMAGE) now goes above 100%: 125%, 150% and
  200% draw the picture larger and scale it down. It needs a lot of video memory; the game lowers the scale by itself
  when the card cannot hold it.
- **Nintendo controller layout.** CONTROLLER LAYOUT: Nintendo (CONTROLS > BINDINGS) is the Xbox layout by the
  buttons' labels: A confirms and boosts, B goes back.
- **Import Xbox textures.** For players who want to use their Xbox disc: the launcher's HD Textures page can build a
  texture pack from your own Xbox disc image. It is a small improvement, since only some textures are larger on the
  Xbox; an HD texture pack looks far better. Your own pack and mods always come first (GRAPHICS > TEXTURES > XBOX
  TEXTURES).
- **Mods without a mod.ini.** The Mods page installs a plain pack of replacement game files as a mod, after asking,
  and **Add folder...** adds a mod from a folder.
- **Optional extra: SURFACE DETAIL** (GRAPHICS > LIGHTING) adds relief and sheen to the road, kerbs and walls. Off by
  default.
- **Portable mode.** PORTABLE MODE (the launcher's Settings > LAUNCHER, or GAME > STARTING AND LOADING in the game)
  keeps your saves, settings, mods and imports in an `appdata` folder beside the launcher, for a USB stick or a second
  install. The launcher moves your data there and back, checks every file, and keeps the old copy until the game has
  run from the new place. Windows and Linux.

## Changed

- **SETTINGS tidied up.** The page now has nine tabs: DISPLAY, GRAPHICS, EFFECTS, CAR, CONTROLS, AUDIO, GAME, SAVES
  and MODS, with each row where you would look for it. Nothing was removed and your settings are kept. A prompt bar
  shows the buttons of your controller or keyboard.
- **Launcher tidied up.** Play fits one window without scrolling, the launcher's own options are on a LAUNCHER tab of
  Settings, Controllers has TEST and BINDINGS tabs, and buttons no longer clip their text in other languages.
- **Saving is safer and clearer.** A small bubble in the top right corner says SAVED each time your file is written,
  and says so when it could not be. A save is written beside the old one and only then replaces it, so a crash or a
  power cut leaves your last whole save. A save file changed outside the game while you play is never written over:
  the launcher's Saves page then lets you choose which to use. A damaged save can be set aside from the same page
  instead of blocking the file.
- **Replays survive a crash.** A recording is written to disk about every 10 seconds of play. Ghosts and best times made
  with gameplay mods are kept apart from your own.
- **Big HD packs need far less video memory.** Compressed pack textures stay compressed on the graphics card.
- **Graphics work prepared at start.** The game builds its shaders when it starts instead of at their first use in
  a race. FRAMES IN FLIGHT: Auto (DISPLAY > SMOOTHNESS) lets the CPU run a frame ahead when the graphics card is the
  limit (in the default latency mode only).
- **Mod safety.** A script mod that writes the game's memory or calls its functions must now say `unsafe = yes` in
  its mod.ini and is asked about once, as code mods are; older scripts that do this stop with a message until their
  author adds it. Mods with a DLL are asked about once more after this update, because the check now covers every
  library in the mod.
- **Launcher updates.** An update can be cancelled, cannot install an older version by mistake, and START GAME waits
  while one installs.

## Fixed

- **Split screen no longer crashes** on Windows some seconds into a two-player race, or on resuming from pause.
  (Issue #9.)
- Split screen is smooth at high frame rates, and player 2's half has its shadows, effects and light glows.
- **Windows: folders with any letters in their names work** (a user name or game folder in Cyrillic, Japanese or
  with accents on a PC set to another language).
- Backing out of the new-profile page, or playing with AUTOSAVE Off, no longer traps you in a save question.
- Watching a replay no longer overwrites your Time Attack ghost or a speedrun best time.
- Round button prompts are round at every screen shape, and one-letter labels are centred.
- The thin wires over the tracks are blended as the PS2 does it; their edges were drawn slightly differently.
- F12 screenshots: no glow left over from the previous shot, SCREENSHOT SAVED appears when the file is written, and
  two shots in one second no longer replace each other.
- An HD pack texture too large for the graphics card showed as white; the game's own texture is used.
- A damaged movie, save import or modded car file can no longer crash the game.
- Window: fixes for minimising, a lost graphics device, the FULLSCREEN toggle and a changed refresh rate.
- Sound: fixes for a device unplugged or changed while playing, and for crackle on devices with large buffers.
- Steam Deck and Flatpak: fixes for the on-screen keyboard and for the data folder inside Steam's Flatpak.
- The launcher's logo is no longer clipped.
- Add to Steam: when Steam is still running, the message now says how to close it on Windows, Mac and the Steam Deck.
  (Issue #12.)

## Issues closed

- **#9** Split-screen crash on Windows.
- **#11** RENDER SCALE above 100%.
- #10 was a question (how to skip a music track), answered in the issue.
