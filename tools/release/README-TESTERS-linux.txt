Burnout 3: Takedown -- native PC port by u0rhs, TEST BUILD (Linux)
Version: @VERSION@

PLEASE DO NOT SHARE THIS BUILD. It is given to you personally for testing. Do not upload it, post it or pass it on.


WHAT THIS IS

A PC port of Burnout 3: Takedown (PlayStation 2) that runs the game natively on Linux: no emulator. It contains
no game data. You need your own, legally owned copy of the US disc:

    Burnout 3: Takedown, NTSC-U, SLUS-21050

The port reads the disc's files (or an image of the disc) from your PC. Nothing is downloaded and nothing is sent
anywhere; bug reports are files you send yourself.


WHAT YOU NEED

- 64-bit Linux on an x86-64 PC (not ARM) with glibc 2.35 or newer: Ubuntu 22.04 or newer, Debian 12, Fedora 36 or
  newer, Arch, SteamOS 3 (Steam Deck), Linux Mint 21 or newer, ...
- A graphics driver with Vulkan, and the Vulkan loader (libvulkan.so.1). Most desktops have both; if not:
      Ubuntu / Debian / Mint:  sudo apt install libvulkan1 mesa-vulkan-drivers
      Fedora:                  sudo dnf install vulkan-loader mesa-vulkan-drivers
      Arch:                    sudo pacman -S vulkan-icd-loader vulkan-radeon   (or vulkan-intel; NVIDIA: nvidia-utils)
  NVIDIA cards need NVIDIA's own driver (the open "nouveau" driver is too slow or has no Vulkan).
- An X11 or Wayland desktop, and sound through PipeWire, PulseAudio or ALSA (any of them).
- The disc's contents: a disc image (.iso, made from your own disc), or every file and folder of the disc copied into
  a folder (SYSTEM.CNF, SLUS_210.50, DATA, TRACKS, PVEH, SOUND, ...)

Everything else the port needs is in this folder (SDL 3 included); it installs nothing and needs no root.


HOW TO START

1. Unpack this archive into a folder of its own, for example in a terminal:
       mkdir -p ~/Games && tar -xzf burnout3-native-*-linux-x86_64.tar.gz -C ~/Games
   (or "Extract here" in your file manager). Keep all its files together.
2. Start b3launcher, in a terminal:  cd ~/Games/burnout3-native-*/ && ./b3launcher
   (some file managers also start it with a double-click; or run ./add-to-menu.sh once and start it from your
   desktop's application menu, below).
3. On the Game Data page, choose your disc image (.iso) or the folder with the disc's files. It remembers the choice.
4. Check the Settings page if you like (window or fullscreen, quality, frame rate limit, ...).
5. Play page: START GAME. The launcher closes once the game runs.

Always start the game with the launcher. To skip its pages, turn on "Start the game directly next time (skip this
launcher)" on the Play page: starting the launcher then starts the game at once. To get the launcher back, hold Shift
(or Back / Select on a controller) while it starts, or start ./b3launcher --launcher, or turn SETTINGS > INTERFACE >
OPEN THE LAUNCHER AT START back on in the game. It also shows by itself when the game data is missing or the last run
did not end cleanly.

Optional: ./add-to-menu.sh puts "Burnout 3 (port test build)" with its icon into your desktop's application menu
(a file in ~/.local/share/applications; ./add-to-menu.sh --remove takes it out again). Run it again if you move
the folder.

The file and folder choosers are your desktop's own (through xdg-desktop-portal, which GNOME, KDE and SteamOS have,
or zenity).

If the game does not start or a run crashes, the launcher's Play page says so the next time.
START IN SAFE MODE starts with low settings (a window, no anti-aliasing, no HD textures, vsync) without changing your
own settings. After two starts in a row that did not end cleanly the game uses safe mode by itself; the Play page has
"Clear the crash count" to stop that.


STEAM DECK (and Steam on any Linux PC)

1. Desktop Mode: unpack the archive (for example into /home/deck/Games) as above.
2. In Steam (Desktop Mode): Games > Add a Non-Steam Game to My Library > BROWSE..., show "All Files", choose
   b3launcher in the folder, ADD SELECTED PROGRAMS. (Or run ./add-to-menu.sh first: then "Burnout 3 (port test
   build)" is in the list to tick.)
3. Back in Gaming Mode it is in your library under Non-Steam. The first start shows the launcher: choose the game
   data (the launcher works with the Deck's controls; the touch screen works as a mouse), then START GAME.
Leave the shortcut's compatibility tool (Proton) off: this is a Linux program. Steam Input's gamepad layout works;
the game sees an Xbox-style pad.


CONTROLS

Gamepad: any controller Linux knows (Xbox, PlayStation, Switch Pro, the Steam Deck's, ...), the PS2 layout.
  The triggers are the pedals (RT accelerate, LT brake), so the PS2's R2 (Crashbreaker) is the right stick
  click (R3 / RS) and its L2 (skip the music track) the left stick click (L3 / LS).

Keyboard (shown on the launcher's Controllers page):
  Driving:  W / Up = accelerate, S / Down = brake / reverse, A D / Left Right = steer,
            Space, Shift or R = boost (held while crashing: Impact Time and Aftertouch), Escape = pause
  Menus:    arrows / WASD = move, Enter, Space or Z = confirm, Backspace = back, Escape = start / pause
  The other PS2 buttons: Q = L1, 1 = L2, E or 3 = R2, X or H = Circle, C = Triangle, Tab = Select
Mouse: the menus can be clicked; the mouse wheel scrolls lists.

  F11 (or Alt+Enter)   fullscreen / window
  F12                  screenshot (into the screenshots folder, see below)
  F10                  make a bug report now (see below)

In the game's main menu, SETTINGS has the PC options (also in the pause menu during a race); QUIT GAME ends the game.

Photo mode: pause a race (also during a crash or a Takedown cam), then press P (gamepad: press both sticks, L3 + R3).
The HUD goes and a free camera takes over: WASD move, E / Q up / down, Shift fast, Alt slow, the mouse or the arrow keys
look, the mouse wheel or - / = zoom, Z / C tilt, R back to the game's view, H hides the help, F12 takes the picture.
Gamepad: left stick move, right stick look, RT / LT up / down, LB / RB tilt, D-pad zoom. P, Escape or B / Circle leave
it; the race is still paused.

Your own music: SETTINGS > INTERFACE > MUSIC. Choose a folder (MP3, FLAC, Ogg Vorbis or WAV; subfolders too) and the
PLAYLIST: EA Trax only (the original, the default), Custom only, or Mixed. Your songs play through the game's radio
(its volume, shuffle and the EA TRAX popup with the song's title / artist / album from its tags). Changes apply from the
next song. Note: a bug report's input recording replays exactly only with the same music settings.

The launcher's STATS page shows each profile's stats from the game's save and 13 achievements worked out from them.


WHERE YOUR FILES ARE

Everything the port writes is in your user data folder:

    ~/.local/share/b3port/native/      ($XDG_DATA_HOME/b3port/native/ when you set XDG_DATA_HOME)
    (a hidden folder: Ctrl+H shows hidden folders in most file managers)

    saves/          your save files (profile-1.sav ...; backups/ keeps older copies)
    pc_settings.ini the PC settings      launcher.ini   the launcher's choices (the game data folder)
    run_state.ini   whether the last run ended cleanly (safe mode)
    logs/           run-<date-time>.log and run-<date-time>.pads.txt for each of your last 10 runs: the game's
                    log and a recording of your controls, so a developer can replay exactly what happened
    screenshots/    F12's pictures

The game's files on your disc copy are only read, never changed.


REPORTING A BUG

1. Note what happened and roughly when (which mode, track, menu).
2. Make a bug report:
   - in the game: press F10. A banner says "BUG REPORT SAVED" and where;
   - or in the launcher: Play page, "Make a bug report". Its folder opens afterwards.
3. Send the bug-report-<date-time>.zip from your Desktop (or Documents, or the user data folder when you have
   neither) with your note.

The report holds: the logs and control recordings of your last 5 runs, your settings files, the version, and a short
system description (Linux distribution and kernel, CPU, RAM, graphics card and driver, display). It holds no game data
and no save file -- unless you set "Include my save: YES" on the launcher's Play page first (useful when the problem
depends on your progress). Your user name is taken out of the folder paths in it.

If the game crashed, just make the report afterwards: the crashed run's log and recording are in it.


IF IT DOES NOT START

- Started from a terminal, ./b3launcher and ./native_play print what went wrong.
- "version `GLIBC_2.35' not found": the distribution is older than this build supports (see WHAT YOU NEED).
- "Permission denied": the archive was unpacked without the programs' execute permission:
      chmod +x b3launcher native_play add-to-menu.sh
- No picture / "no GPU" / the game window closes at once: check Vulkan (vulkaninfo --summary, from the package
  vulkan-tools; see WHAT YOU NEED), then send a bug report from the launcher.


MORE HELP

The player guide, docs/PLAYING.md in the project's source repository, goes through all of this in more detail:
making a disc image of your own disc, the launcher's pages, controls and remapping, saves and PS2 save import, the
settings, mods, and troubleshooting.


KNOWN ISSUES

See KNOWN-ISSUES.txt. Please still report anything there you run into, with a bug report.


THIRD-PARTY SOFTWARE

See the LICENSES folder.
