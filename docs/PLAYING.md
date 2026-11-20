# Playing Burnout 3: Takedown on PC

This guide covers everything you need to play: what you need, how to install the port, the launcher, the controls,
saves, settings, mods, and what to do when something goes wrong. If you want to build the port yourself or work on
it, see [CONTRIBUTING.md](../CONTRIBUTING.md). If you want to make mods, see the [modding guide](modding/README.md).

**The game is not included.** This project never supplies the game, its files or a disc image, and never links to
places that do. You play from your own copy of the disc. The port reads your copy and never changes it.

- [1. What you need](#1-what-you-need)
- [2. Making a disc image of your own disc](#2-making-a-disc-image-of-your-own-disc)
- [3. Install and first run](#3-install-and-first-run)
- [4. The launcher](#4-the-launcher)
- [5. Controls](#5-controls)
- [6. Saves](#6-saves)
- [7. Settings worth knowing](#7-settings-worth-knowing)
- [8. Mods](#8-mods)
- [9. Troubleshooting and bug reports](#9-troubleshooting-and-bug-reports)
- [10. Known issues](#10-known-issues)

## 1. What you need

### Your own copy of the game

**Burnout 3: Takedown for PlayStation 2, the US release (NTSC-U, SLUS-21050).** Other regions and versions do not
work. The launcher checks the game program on your disc and tells you if it is a different one.

You can give the port the game in either of two forms:

- **a disc image** of your disc: an `.iso` file, a raw `.bin`, or a `.chd` (as `chdman` makes it from either). It is
  read in place and nothing is unpacked.
- **a folder** with every file and folder of the disc copied into it: `SYSTEM.CNF`, `SLUS_210.50`, `DATA`, `TRACKS`,
  `PVEH`, `SOUND`, `OVID` and the rest.

Section 2 shows how to make either one from your disc.

**Other languages.** The game runs from the US disc, in English. If you also own a European copy (PAL), the launcher's
**Settings > GAME > LANGUAGE AND UNITS > IMPORT LANGUAGES** copies its languages once (French, German, Italian, Spanish, Dutch, Swedish
or UK English, whichever your disc has; two small files each), and **LANGUAGE** then switches the game's menus,
messages and hints to one of them, from the next start. The launcher and the port's own SETTINGS page follow the same choice (a few live status lines stay in English). English
stays the default; the European disc is not needed afterwards.

**The DJ in your language.** The European discs also have the Crash FM DJ in French, German and Italian (the
Fr, De, It disc) and Spanish (the En, Es, Nl, Sv disc). After you choose the disc in IMPORT LANGUAGES, a **DJ VOICE**
row per language offers to copy the DJ's lines too (opt-in; 290 to 360 MB each, the size is shown, and the launcher checks
the free space first). The copy can be cancelled; nothing is kept then. **Remove** on the same row deletes it again.
With LANGUAGE set to that language the DJ speaks it from the next start. The race training's narrator speaks French,
German, Italian or Spanish too, with nothing to import (the US disc has those voices); its pictures with their
on-screen text are a second optional import, **TRAINING VIDEOS** (about 130 MB), and without them the training shows
the English pictures. **VOICES: English** keeps the US voices with translated menus. UK English, Dutch and Swedish
have the English voices, as on their own discs.

### Your PC

| | Windows | Linux | Mac |
|---|---|---|---|
| System | Windows 10 or 11, 64-bit | 64-bit Linux on an x86-64 PC (not ARM), glibc 2.35 or newer: for example Ubuntu 22.04 or newer, Debian 12, Fedora 36 or newer, Linux Mint 21 or newer, Arch, SteamOS 3 (Steam Deck) | A Mac with Apple Silicon (M1 or later); the macOS version the app's `README-TESTERS.txt` names |
| Graphics | A graphics card and driver with **Vulkan** (1.0 or newer) | A graphics driver with **Vulkan** (1.0 or newer), and the Vulkan loader (`libvulkan.so.1`) | Metal (every Apple Silicon Mac has it) |
| Other | | An X11 or Wayland desktop; sound through PipeWire, PulseAudio or ALSA | Nothing else: SDL 3 is inside the app |
| How you get it | A zip from the [Releases](../../../releases) page | A tarball from the [Releases](../../../releases) page | A disk image (`.dmg`) from the [Releases](../../../releases) page, or build it yourself (section 3) |

The exact minimum CPU, memory and graphics card are **not yet measured**. As a guide: on a current desktop PC (an
RTX 4080 SUPER) the game draws several hundred frames per second in a window, far above the game's own 60. On a slow
PC a race can fall behind real time, and the sound then stutters; section 9 says what to try.

The download itself is small (the test builds were about 23 MB). Your disc image or folder is the big part, and it
stays where you put it.

## 2. Making a disc image of your own disc

You need a PC with a DVD drive and your own Burnout 3 disc. There are two ways, and both work equally well.

**The easiest way: copy the files.** Put the disc in the drive, open it in your file manager (Explorer, Finder, or
your Linux file manager), and copy *everything* on it into a new, empty folder, for example `Burnout3`. Then choose
that folder in the launcher.

**Or make a disc image (`.iso`):**

- **Windows:** use a disc imaging program that can save a DVD as an `.iso` file ("create image from disc" or "read
  disc to image"). Many free ones exist. Choose the `.iso` format.
- **Linux:** find the drive's name with `lsblk` (usually `/dev/sr0`), then in a terminal:
  ```sh
  dd if=/dev/sr0 of=~/Burnout3.iso bs=2048 status=progress
  ```
- **Mac:** a **folder** is the tried way on a Mac, so copy the disc's files in Finder as above. If you already have an
  `.iso` of your disc, double-click it to open it and copy its files into a folder. (The Mac launcher also offers
  Choose disc image..., but an image has not been tried on a Mac yet; the build-it-yourself steps take a folder only.)

When it is done, the launcher's Game Data page checks your image or folder: every needed file gets a green tick. If
the copy was cut short (an interrupted copy, a scratched disc), it says the image is incomplete and how much of it is
there. Make the image again in that case.

## 3. Install and first run

### Windows

1. Download `burnout3-native-<version>-win64.zip` and unzip it into a folder of its own, for example
   `Documents\Burnout3Port`. Keep all its files together.
2. Start **`Burnout3Launcher.exe`**.
   - Windows may warn that the program is from an unknown publisher (SmartScreen), because the build is not signed.
     Choose **More info**, then **Run anyway**.
3. Open the **Game Data** page and press **Choose disc image...** (your `.iso`, `.bin` or `.chd`) or **Choose
   folder...** (the folder with the disc's files). The launcher remembers your choice.
4. If you like, look through the **Settings** page: window or fullscreen, quality, frame rate limit, and more.
5. On the **Play** page, press **START GAME**. The launcher closes once the game runs.

### Linux

1. Download `burnout3-native-<version>-linux-x86_64.tar.gz` and unpack it into a folder of its own, either with
   "Extract here" in your file manager or in a terminal:
   ```sh
   mkdir -p ~/Games && tar -xzf burnout3-native-*-linux-x86_64.tar.gz -C ~/Games
   ```
2. Start **`b3launcher`**: in a terminal, `cd ~/Games/burnout3-native-*/ && ./b3launcher`. Some file managers also
   start it with a double-click.
3. Optional: run **`./add-to-menu.sh`** once. It puts a Burnout 3 entry with its icon into your desktop's application
   menu, and so also into Steam's "Add a Non-Steam Game" list. Run it again if you move the folder, and
   `./add-to-menu.sh --remove` takes the entry out again. It needs no root and installs nothing else.
4. Then as on Windows: the **Game Data** page, then **START GAME** on the **Play** page.

Everything the port needs ships in the folder, including its own copy of the SDL 3 library, so there is nothing to
install. With the mouse or the keyboard, the file and folder pickers are your desktop's own: they need
`xdg-desktop-portal` (GNOME, KDE and SteamOS have it) or `zenity`. Without them, and with a controller, the launcher
uses its own folder list (section 4).

**Steam Deck (and Steam on any Linux PC):**

1. In Desktop Mode, unpack the archive, for example into `/home/deck/Games`.
2. In Steam: **Games > Add a Non-Steam Game to My Library > BROWSE...**, show "All Files", choose `b3launcher` in the
   folder, then **ADD SELECTED PROGRAMS**. If you ran `./add-to-menu.sh` first, the Burnout 3 entry is in the list
   to tick instead. Or, with Steam closed, press **Add to Steam** on the launcher's **Settings** page (its LAUNCHER tab): it adds the
   entry with its cover, background and logo ([Adding the game to Steam](#adding-the-game-to-steam)).
3. In Gaming Mode it is in your library under Non-Steam. The launcher works with the Deck's controls alone, and the
   touch screen works too. Choose your game data, then START GAME.

On a Steam Deck the launcher's first start sets the **MEDIUM** preset with **VSYNC** on, which holds the Deck's 60 Hz
screen (HIGH runs at about 55 there). Change them in Settings like any other setting; a lower **RENDER SCALE** with
**UPSCALING Sharp** (Settings > GRAPHICS) frees more of the GPU.

You need no keyboard or mouse in Gaming Mode. If your disc image or folder is not found by itself, on the **Game Data**
page press **A** on **Choose disc image...** or **Choose folder...**: the launcher's own folder list opens (a desktop
file dialog cannot be used with a controller there). **Home**, the root folder and your SD card or USB drive
(under `/run/media`) are on its left as starting points; press **Y** to go there. Open folders with **A**, go up with
**B**, and press **Start** to choose the folder shown (or **A** on an `.iso` / `.bin` / `.chd` file). The same works
for the Music and HD Textures folders. "Type a path..." opens an on-screen keyboard; Steam's own keyboard may open over
it, and either one works (section 4).

Leave the shortcut's compatibility tool (Proton) **off**: this is a Linux program. Steam Input's gamepad layout works,
and the game sees an Xbox-style pad.

### Mac (Apple Silicon)

1. Download `burnout3-native-<version>-macos-arm64.dmg` and open it. Drag **Burnout 3 Native** onto the
   **Applications** folder beside it, then eject the disk image.
2. **The first start.** The app is not notarized by Apple (it has no Apple developer signature), so macOS blocks it
   the first time:
   - **macOS 15 (Sequoia) and newer:** open the app once from Applications. macOS says it cannot check it: click
     **Done**. Then open **System Settings > Privacy & Security**, scroll down to *"Burnout 3 Native.app" was
     blocked*, click **Open Anyway** and confirm.
   - **macOS 14 and earlier:** the same works, or right-click (Control-click) the app and choose **Open**, then
     **Open** again.
   - **Or, on any version,** once in Terminal:
     ```sh
     xattr -dr com.apple.quarantine "/Applications/Burnout 3 Native.app"
     ```
   After that it opens with a normal double-click. Each new version needs this once again.
3. The launcher opens: then as on Windows, the **Game Data** page (choose the folder with the disc's files, section
   2), then **START GAME** on the **Play** page.

Everything the port needs is inside the app, including its own copy of the SDL 3 library: no Homebrew, nothing else
to install. To update, drag the new version onto Applications and replace the old one; your saves and settings are in
the user data folder (section 6) and stay. The readme, the known issues and the licenses are inside the app
(right-click > **Show Package Contents** > Contents > Resources). More on the Mac app, and on building it yourself:
[docs/mac.md](mac.md).

**Or build it from source** (the developers' way, and for a Mac the download does not cover). The steps are in
[docs/mac.md](mac.md):

1. **One-time setup** ([section 1](mac.md#1-one-time-setup)): the Xcode command line tools
   (`xcode-select --install`), then [Homebrew](https://brew.sh) and `brew install sdl3 pkg-config make`. Then get the
   source code of this project (`git clone`).
2. **Copy your disc's files into a folder** (section 2 above).
3. **Build and play** (section 9 of [docs/mac.md](mac.md)): from the project folder, run
   ```sh
   tools/mac/native.sh <your disc folder>
   ```
   The first build compiles about 4,900 files, which takes roughly 10 to 20 minutes on an M1. After that, the same
   command only rebuilds what changed and starts the game. Close the window to quit.

   These steps start the game directly, without the launcher (`LAUNCHER=1 tools/mac/native.sh` opens the launcher
   instead). `tools/release/package_mac.sh` builds the same app as the download (docs/mac.md section 10).

## 4. The launcher

The launcher sets everything up before you play, so you do not need to start the game for it. Once it finds your
disc it takes on the look of the game's own menus. It works with the mouse, the keyboard or a gamepad, each on its
own. It only changes the port's own files, never your disc.

- **Up / Down** (arrow keys, D-pad or left stick) move between the rows of a page. Where a row holds several values
  side by side (the Controllers page's player and device, the Showroom's paint), Up / Down go through them one at a
  time. Down on the last row shows the rest of the page.
- **Left / Right** change the value of the row you are on, or move along a row of buttons.
- **Enter / A** presses, **Esc / B** goes back to the list of pages, **Tab / LB / RB** change the page, and
  **PgUp / PgDn / LT / RT** change the Settings tab or the Showroom's car.
- With the mouse, click a row's value on its left half to step it back, and use the wheel to scroll.

**With a controller only** (a Steam Deck in Gaming Mode, a PC by the TV), everything works without a keyboard or a
mouse:

- **Choose folder...**, **Choose disc image...** and the other file buttons open the launcher's own **folder list**
  instead of your system's file dialog. Up / Down move through the folders, **A** opens one (or chooses a file of the
  kind asked for), **B** goes up a folder, **Start** chooses the folder shown, **Y** jumps to the places on the left
  (the current choice, your home folder, the drives, and SD cards and USB drives), **X** types a path instead and
  **Select / Back** cancels. LB / RB and LT / RT move a page at a time. The mouse works on it too: click a row twice
  to open it.
- **Type a path...** opens an **on-screen keyboard** under the field. The D-pad or the stick moves, **A** types the
  key, **B** deletes (on an empty field it closes), **X** is a space, **Y** switches between lower case, capitals and
  symbols, **LB / RB** move the cursor, and **Start** (or the Done key) confirms; **Select / Back** cancels. Its keys
  can also be clicked or touched, and a keyboard still types. Where the system has its own on-screen keyboard (Steam's,
  in a Steam Deck's Gaming Mode), that one opens over it as well, and you can use either; the launcher's "System
  keyboard" key brings Steam's back.
- A touch on the screen counts as a controller here: the folder list and the on-screen keyboard open for it too. In
  Steam's Gaming Mode and Big Picture the folder list is used for every input. Elsewhere, with the mouse or the
  keyboard you get your system's file dialog, and you type as usual; the small keyboard mark at the right of a text
  field opens the on-screen keyboard any time. (`B3LAUNCHER_FILE_DIALOG=launcher` or `=system` picks one for every
  input.)

| Page | What it is for |
|---|---|
| **Play** | **START GAME**, or **START IN SAFE MODE** (section 9). It shows whether your game data, save file and settings are in order, how the last run ended, and which mods are on. It also has **Start the game directly next time** (below), **Bug report...** (section 9) and **Quit**, and says when a newer version of the port is out. |
| **Game Data** | Your disc image or folder, with a check of every file the game needs. |
| **Saves** | Your save files: which one the game uses, export, and importing PS2 saves (section 6). |
| **Settings** | The same settings as the game's own SETTINGS page, in the same tabs, saved to the same file; the controls, the music folder and the HD pack have a row that opens their own launcher page. The last tab, **LAUNCHER**, holds the launcher's own: **OPEN THE LAUNCHER AT START**, **CHECK FOR UPDATES** (see below), **Add to Steam** and **Reset all settings**. |
| **Controllers** | Two tabs: **TEST**, your connected controllers live, with a button test and a rumble test; **BINDINGS**, the keys and buttons for each player, which you can change (section 5). |
| **HD Textures** | Choose an HD texture pack. Packs made for PCSX2 work as they are: choose the pack's `SLUS-21050` folder, or the folder above it. It is used from the next start. In the game, F4 switches the pack off and on at once, to compare. **Import Xbox textures...** builds a pack on your PC from your own Xbox disc image of the game (`.iso` or `.xiso`), for players who want to use their Xbox disc. It is a small improvement: some building fronts, signs and car paint are somewhat sharper, in the PS2 colours; an HD texture pack looks far better. It takes about a minute and about 90 MB, and nothing is downloaded. Your own HD pack and the mods' textures come first; XBOX TEXTURES (Settings > GRAPHICS > TEXTURES) turns the import off, F4 switches it with the rest, and **Remove** deletes it. |
| **Music** | Your own music on the game's radio (section 7). |
| **Mods** | Install mods, turn them on or off, and set their order (section 8). |
| **Stats** | Each profile's stats as the game saved them, and 13 achievements worked out from them. |
| **Showroom** | Every player car on your disc in 3D on a turntable, with the game's names. Drag or use the right stick to turn it, the mouse wheel to come nearer or go back, and left / right for the next car. The cars use the same HD textures as the game: your HD pack when HD TEXTURES is on, and the textures of the mods you have turned on. |
| **Replays** | Sessions you recorded (turn on **RECORD REPLAYS** first: section 7). Play one back, exactly as it was played, or delete it. |
| **Credits** | Who made the port, and the people and projects it builds on. It also shows your version and whether it is up to date. |

### Update check

Each time you open it, the launcher checks in the background whether a newer release of the port is out (a restart
within a few minutes reuses the last answer). If one is, the Play page shows **Update available** with the new
version's name and two buttons:

- **Install update** downloads the package for your system, checks it against the release's signed list of files
  (`SHA256SUMS` and `SHA256SUMS.sig`, signed with the port's release key), and installs it over your current copy (the
  old version is kept, for Undo);
  **Restart now** then starts the new version. On a Mac it opens the checked disk image: drag the app to Applications
  as the first time. If the download or its signature does not check out, nothing is installed.
- **Release page** opens the release in your browser, to download it yourself.

Your settings and saves stay where they are, in your user data folder (in portable mode the `appdata` folder beside
the launcher, which an update never touches). **Undo the last update** on the Settings page's LAUNCHER tab
goes back to the version you had before. The Credits page shows the result: "Up to date", or "Couldn't check for
updates" when there was no answer (no internet, for example). Nothing else changes when the check fails.

**Privacy:** the check is one request to GitHub, where the port is published
(`https://api.github.com/repos/u0rhs/burnout3-native/releases/latest`). As with any web page you open, GitHub sees your
IP address. Nothing else is sent: no name, no email, no information about your PC, your game or your saves; the
request names only the launcher and its version. Install update downloads only the release's own files from
GitHub. To turn it off, set **CHECK FOR UPDATES** to Off on the
**LAUNCHER** tab of the launcher's **Settings** page: the launcher then makes no network request at all. **Check now** beside it checks at
once.

### Adding the game to Steam

**Add to Steam**, on the **LAUNCHER** tab of the launcher's **Settings** page, puts Burnout 3: Takedown into your Steam library as
a non-Steam game, with its own cover, background, logo and icon, so that it looks right in the library, in Big
Picture mode and on a Steam Deck. The entry starts the launcher (`Burnout3Launcher.exe`, `b3launcher`, or the app on a
Mac) in its own folder.

1. **Close Steam first.** On Windows, right-click the Steam icon by the clock and choose **Exit**; on a Mac, **Steam >
   Quit Steam**. On a Steam Deck (in Desktop Mode), right-click the Steam icon in the taskbar (bottom right) and choose
   **Exit Steam**. Steam keeps its list of games in memory and writes it back when it closes, which would drop the new entry, so the
   launcher adds nothing while Steam runs, and says so.
2. Press **Add to Steam**. Every Steam account that has signed in on this computer gets the entry. Your other
   non-Steam games stay exactly as they were; the list as it was before is kept beside it as `shortcuts.vdf.b3backup`
   (in Steam's `userdata/<account>/config/` folder).
3. Start Steam: Burnout 3: Takedown is in your library.

Pressing it again adds no second entry: it puts the pictures in again. If you move the port's folder, press it again
from the new place, and remove the old entry in Steam (it still points to the old folder).

**Your own pictures.** The port's pictures are made from its logo; the port never ships the game's box art. To use
pictures of your own (a photo of your own box, for example), choose **Cover image...**, **Background image...** or
**Logo image...** (PNG or JPEG files), then press **Add to Steam**; **Use the port's art** goes back to the port's.
What fits best: a 600 x 900 cover, a 1920 x 620 (or 3840 x 1240) background, a logo as a PNG with a transparent
background. Steam's own **Manage > Set custom artwork** works on the entry too.

Started from Steam, the game also gets Steam Input and the Steam overlay, and on Linux Steam's desktop controller
layout no longer clicks around the desktop while you play (section 9, Common problems).

Where it looks for Steam: on Windows, the folder the registry names, else `C:\Program Files (x86)\Steam`; on Linux
and the Steam Deck, `~/.local/share/Steam` (also reached as `~/.steam/steam`) and Flatpak's
`~/.var/app/com.valvesoftware.Steam`; on a Mac, `~/Library/Application Support/Steam`. Flatpak's Steam also needs to
be allowed to see the port's folder: the launcher then shows the `flatpak override` command for it.

**Steam's Flatpak.** The Flatpak of Steam starts the game inside its own sandbox, where the user data folder is
`~/.var/app/com.valvesoftware.Steam/data/b3port/native` instead of `~/.local/share/b3port/native`: started from that
Steam, the launcher and the game would have settings, saves and mods of their own (the launcher says so the first
time). To use the same data inside and outside Steam, let Steam's Flatpak see your data folder once, in a terminal:

```
flatpak override --user --filesystem=~/.local/share/b3port com.valvesoftware.Steam
```

From then on, started from Steam, the launcher and the game use `~/.local/share/b3port/native` as they do outside
Steam. Settings the sandbox's folder got on the first run (before the command) stay there, unused; the launcher says
so once. Two exceptions keep the sandbox's own folder: a read-only grant (`:ro`; saves could not be written), and a
sandbox folder that already holds a save (saves are never left behind: when both folders hold data the launcher says
so once). To move over, copy what you want to keep from `~/.var/app/com.valvesoftware.Steam/data/b3port/native` into
`~/.local/share/b3port/native`, then delete the former.

### Starting the game without the launcher

Always start the game with the launcher. If you do not need its pages every time, turn on **Start the game directly
next time (skip this launcher)** on the **Play** page. From then on, starting the launcher shows "Starting Burnout
3..." for a second and starts the game with your settings, saves, mods and music, as START GAME does.

To get the launcher back:

- **Once:** hold **Shift**, or **Back / Select** on a controller, while you start the launcher (or while "Starting
  Burnout 3..." shows).
- **For good:** in the game, turn **SETTINGS > GAME > STARTING AND LOADING > OPEN THE LAUNCHER AT START** back on (or the switch on the
  Play page, once the launcher shows). It takes effect the next time you start.

The launcher also shows by itself when something needs you: no game data (or it is missing), the game program is not
beside it, or the last run did not end cleanly. The Play page then says why it opened. Starting it with `--launcher`
(for example `./b3launcher --launcher`) always shows it.

## 5. Controls

You can use the keyboard, the mouse in the menus, and any gamepad your system knows (Xbox, PlayStation, Switch Pro,
the Steam Deck's, and others). The game's button prompts change to match whatever you used last: PlayStation,
PS4 / PS5, Xbox, Nintendo, a generic pad, or keyboard keys. To fix one style, go to SETTINGS > GAME > INTERFACE > BUTTON
PROMPTS.

### Keyboard (defaults)

The same key can do one thing while you drive and another in the menus.

| Key | While driving | In the menus |
|---|---|---|
| W or Up | accelerate | move up |
| S or Down | brake / reverse | move down |
| A / D or Left / Right | steer | move left / right |
| Space | boost (hold it as you crash for Impact Time and Aftertouch) | confirm |
| Left Shift or R | boost | |
| Enter, keypad Enter or Z | accelerate | confirm |
| Escape | pause | Start (and resume) |
| Backspace or C | change camera | back |
| Q | look back | |
| E or 3 | Crashbreaker | |
| X or H | horn | |
| 1 | skip the music track | |
| V | | car colour, change names |
| Tab | Select | Select |

While you crash, the steering and up / down keys steer the wreck (Aftertouch).

The **mouse** works in the game's menus. Point at a row to select it, left-click to choose it, right-click to go
back, and use the wheel to scroll. You can also click the prompts at the bottom of the screen. In a race the mouse
does nothing. On the profile name screen you can **type** the name with your keyboard.

### Gamepad (defaults)

Each controller gets the layout of its own console: a PlayStation controller (PS3, PS4, PS5) the PS2 game's, and
every other controller (Xbox, Switch, the Steam Deck, generic pads) the original Xbox game's. This is **CONTROLLER
LAYOUT: Auto**, and it picks again when you change controllers; two players with different controllers each get
their own. The CONTROLS settings show which one is in use ("Auto: Xbox controller").

| | PlayStation layout | Xbox layout | Nintendo layout (chosen only) |
|---|---|---|---|
| Accelerate | Cross or R2 (analog), or the right stick up | right trigger (analog) | ZR (analog where the pad has it) |
| Brake / reverse | Square or L2 (analog), or the right stick down | left trigger (analog) | ZL |
| Steer (and Aftertouch in a crash) | left stick or D-pad | left stick or D-pad | left stick or D-pad |
| Boost (hold it as you crash for Impact Time) | R1 | A (RB too) | A (R too) |
| Crashbreaker | press the right stick (R3) | B | B |
| Look back | L1 | X (LB too) | X (L too) |
| Change camera | Triangle | Y | Y |
| Skip the music track | press the left stick (L3) | View / Back | - |
| Horn | Circle | press the left stick | press the left stick |
| Pause | Start / Options | Start / Menu | + |
| Menus: move | left stick or D-pad | left stick or D-pad | left stick or D-pad |
| Menus: confirm | Cross | A | A |
| Menus: back | Triangle | B | B |
| Menus: car colour, change names | Square | X | X |

On the Xbox layout A and B do one thing while you drive and another in the menus, as in the original Xbox game. In a
crash A (Impact Time, Aftertouch) and B (the Crashbreaker) both work. The triggers are the pedals in the menus too, so
you can rev on the grid, and they work gradually, like real pedals: light pressure, light throttle (SETTINGS >
CONTROLS > DRIVING > ANALOG TRIGGERS, on by default; off makes a trigger act as a button). Both triggers together still
powerslide. The PlayStation layout is the PS2's with the triggers as the pedals too (R2 accelerates and L2 brakes, as
well as Cross and Square), so the PS2's R2 (Crashbreaker) and L2 (music track skip) are on the stick clicks, R3 and
L3; the game's button prompts show R3 and L3 for them. The right stick is the game's own analog throttle and brake.

On **Auto** a Nintendo controller gets the Xbox layout **by position**: its bottom button (B on a Switch controller)
boosts and confirms, and its right button (A) is the Crashbreaker and goes back. Choose **CONTROLLER LAYOUT: Nintendo**
for the Xbox layout **by label** instead (the table's last column): A (the right button) confirms and boosts, B (the
bottom one) goes back and is the Crashbreaker, and X / Y swap the same way. The button prompts follow the layout.

**CONTROLLER LAYOUT** (SETTINGS > CONTROLS > BINDINGS with DEVICE: Controller, or the launcher's Controllers page),
per player:

- **Auto** (the default): the layout of the controller's console, as above.
- **Xbox** or **PlayStation**: that layout on any controller.
- **Nintendo**: the Xbox layout by label (above), on any controller. Auto never picks it.
- **Custom**: your own bindings. Changing any controller binding switches to Custom (starting from the layout you
  were on); choosing Auto, Xbox, PlayStation or Nintendo again keeps your Custom bindings for later.

Controls you changed in an earlier version stay exactly as they were (Custom). If you never changed a controller
binding, you get Auto. Before 1.2.1 every controller had one layout: the PlayStation layout above without the right
stick; Custom without changes is still that layout. The keyboard is not
affected by any of this.

### Looking around the car in the garage

On the pages that show your car on its turntable (SELECT CAR, and YOUR GARAGE in Driver Details), the **right stick**
turns the view around the car: left / right to go round it, down to look from a little higher (up brings it back; as
in the launcher's Showroom). With the mouse, **drag** on the car with the left button; a click without a drag still chooses
the car. While you turn the view the car stops spinning, and a second after you let go it starts spinning again from
where you left it (a stick held still part-way, or one that rests a little off centre, lets the spin come back after a
second too; push it further or move it to turn the view again). The distance stays the game's (there is no zoom) and the height is limited, so the camera never
goes into the car, the cones or the ceiling. This works with every controller layout: the game's menus never use the right stick (on the PlayStation layout
it is still the throttle while you drive). To turn it off: SETTINGS > CAR > GARAGE > GARAGE CAMERA.

### Other keys in the game

These are the defaults. Each one can be moved to any key, and most to a controller button too (next section).

| Key | What it does |
|---|---|
| F11 or Alt+Enter | switch between fullscreen and a window |
| F12 | take a screenshot (section 6 says where it goes) |
| F3 | change the FPS counter's view: minimal, standard, detailed, off |
| F4 | switch the HD textures off and on at once, to compare them with the original ones (only when an HD texture pack is in use; a note at the top of the screen says HD TEXTURES ON or OFF). It lasts until the game closes: the HD TEXTURES setting stays as you set it |
| F10 | make a bug report (section 9) |
| F9 | the tuning overlay in a race, only when **DEVELOPER MODS** is on (section 7) |
| P (gamepad: L3 + R3) | photo mode, while the game is paused (the PHOTO MODE row in the pause menu does the same; section 7) |
| F5 (gamepad: Select + Start) | restart the event, only when **INSTANT RESTART** is on (section 7) |
| Page Up / Page Down (gamepad: the right stick up / down) | the previous / next section of a long tab in the settings (the prompt bar at the page's foot shows the buttons for the device in use) |
| Enter, Space or Escape (gamepad: Cross / A or Start) | skip the video that is playing |

### Changing the controls

Go to **SETTINGS > CONTROLS > BINDINGS** in the game, or the launcher's **Controllers** page. Both edit the same
`controls.ini` file in your user data folder (section 6). Every control can be moved to any key or button you like.

- Choose the **PLAYER** (1 or 2, or **Shortcuts**) and the **DEVICE** (keyboard or controller). For a controller,
  choose its **CONTROLLER LAYOUT** (above). Then choose whether you are changing the keys or buttons for **Driving**
  or for **Menus**. **Shortcuts** lists the keys above
  (restart, fullscreen, screenshot, bug report, the tuning overlay, the settings sections), photo mode's controls and
  the controller buttons for them. A controller shortcut is every input of it held at once, like Select + Start:
  Confirm sets the first one, and Right adds another to hold with it.
- Pick a control, then press the new key (any key: letters, function keys, the keypad, Shift, Ctrl, Alt) or, for a
  controller, a button, or a stick or trigger pushed past half way. Escape, or waiting 6 seconds, cancels, so Escape is
  the one key a control cannot be moved to (it still pauses, closes pages and leaves photo mode).
- In the launcher, **Right** on a control adds one more input to it and **Left** removes the last one. With the
  mouse, right-click the control for the same choices.
- If the key is already in use, you can **swap** the two, **clear** the old one, or **keep both**. This includes the
  shortcuts: a key that restarts the event cannot also be asked to do something else without the question.
- To put one control back to its original key or button: **Delete** (or X / Square on a controller) in the launcher,
  **Square** in the game. **RESET TO DEFAULTS** brings back the original layout for that player and device (or for
  the shortcuts of that device), and **RESET ALL CONTROLS** everything (it asks first).
- The button prompts in the game follow your new bindings.
- Alt+Enter always switches fullscreen as well, whatever the FULLSCREEN key is, and the launcher's own menus are
  always moved with the arrow keys, Enter, Escape and Tab (and the D-pad, A and B), so that the page that changes the
  controls can always be reached. The in-game page follows your bindings (it reads the same buttons the game does),
  and the mouse works in it too, so clear a control freely: RESET ALL CONTROLS undoes it.

### Two players

A second gamepad is **player 2**, for the game's local multiplayer (the split-screen races and the other two-player
modes). The first gamepad connected is player 1 (together with the keyboard and the mouse), and the second one is
player 2. With one gamepad or none, nothing changes. Unplugging player 2's pad is the same as pulling a pad out of the PS2's second port, and the game says so in
its own way. Plug it back in to carry on. Player 2 can also play on the keyboard: give player 2 some keys in the
controls settings (they have none at first).
**SPLIT SCREEN** (SETTINGS > DISPLAY > LAYOUT) puts the two players' views top / bottom (the game's own, the default) or side by side.

## 6. Saves

The port does not need a memory card. Each **save file** works like its own memory card, and holds the game's
**three profiles**. There are up to five save files: `profile-1.sav` to `profile-5.sav`. The game saves by itself, as
on the PS2, and without the memory card messages. After a race it does not show the **AUTOSAVE** page either (the
game goes straight on to the next page): in the game, SETTINGS > GAME > STARTING AND LOADING > **SAVE SCREENS** is **Hide** by default;
**Show** brings the page back as on the PS2. Your save is the same either way.

### Where your files are

Everything the port writes goes into your **user data folder**:

| System | User data folder |
|---|---|
| Windows | `%APPDATA%\b3port\native\` (paste this into Explorer's address bar) |
| Linux | `~/.local/share/b3port/native/` (or `$XDG_DATA_HOME/b3port/native/` if you set `XDG_DATA_HOME`). It is a hidden folder: Ctrl+H shows hidden folders in most file managers. |
| Mac | `~/Library/Application Support/b3port/native/` |

In **portable mode** (Windows and Linux, below) it is instead the `appdata\native\` folder beside the launcher. The
launcher's Play page shows which folder is in use ("Data folder").

Inside it:

| File or folder | What it holds |
|---|---|
| `saves/` | your save files (`profile-1.sav` ...), `backups/` with older copies (unless you chose another save folder, below) |
| `pc_settings.ini` | the port's settings |
| `controls.ini` | your key and button bindings |
| `car_look.ini` | only if you tuned the car reflection look with DEVELOPER MODS on (docs/car-look.md) |
| `launcher.ini` | the launcher's choices (your game data, the save folder if you chose one, the update check and its last answer) |
| `run_state.ini` | whether the last runs ended cleanly (after two in a row that did not, the game starts in safe mode) |
| `logs/` | a log and a controls recording for each of your last 10 runs |
| `screenshots/` | the pictures F12 takes |
| `replays/` | recorded sessions, when RECORD REPLAYS is on |
| `ghosts/` | your best Time Attack laps, when GHOST is on (`modded-<set>/`: made with gameplay-changing mods, section 8) |
| `speedrun/` | your best times, when SPEEDRUN TIMER is on (`modded-<set>/` likewise) |
| `mods/` | installed mods |
| `mods.ini` | which mods are on, and their order |
| `updates/` | an update's download while the launcher checks and installs it |

`portable-move.ini` appears after a portable mode move (it names the old copy kept), `xbox-textures/` after an Xbox
texture import, `languages/` after IMPORT LANGUAGES, and `warm_cache.txt` once an HD pack has been used.

### Portable mode (a USB stick)

**PORTABLE MODE** on the **LAUNCHER** tab of the launcher's **Settings** page keeps everything in the list above in a
folder called `appdata` beside the launcher instead, so the whole game (program, data and saves) can be carried on a
USB stick or kept in one folder. Windows and Linux only: a Mac app is never written into, and an AppImage or a Flatpak
cannot be, so the row is not shown there. When the install folder cannot be written by you (Program Files, `/opt`),
the row is shown greyed out with the reason: move the game to a folder of your own first.

Turning it on or off moves your data, once you say yes:

- everything is **copied** first (to `appdata.partial`, or `native.partial`), every file is **checked** against the
  original, and only then put in place. If anything fails, or you press Cancel, nothing changes and your data stays
  where it was. A move cut short (a crash, the power) leaves only the `.partial` folder, removed at the next start;
- the folder your data came from is **kept**, renamed with the date (`native.moved-<date>` or `appdata.moved-<date>`).
  Once the game has started from the new folder and ended normally, the launcher offers to remove it (or remove it
  yourself later with **Remove the old copy...** on the same tab);
- turning it off when this computer's user folder already holds data from an earlier time: you choose which to keep;
  the other is set aside under a dated name (`native.aside-<date>` or `appdata.moved-<date>`). The two are never mixed;
- a save folder you chose elsewhere (on the Saves page, a cloud folder say) is not moved: it stays where it is.
- the game must not run during a move: the launcher holds the save folder's lock meanwhile (a game started from Steam
  is told the launcher is moving your data), and if anything writes to your data while it is copied the move stops and
  nothing changes. Close the game and try again;
- on Windows a path longer than 259 characters cannot be written: if a file would end up with one (a deep mod folder
  in a deep install folder), the launcher names it and moves nothing; move the game to a shorter folder first.

Folders you chose inside the game's folder (the game data, an HD pack, a music folder, a save folder) are remembered
relative to it, so the stick still works when Windows gives it another drive letter (E: on one computer, F: on the
next), or when you move the whole game folder. Folders elsewhere are remembered as they are.

The list of code mods you have allowed is in the data folder too, so it travels with the stick: on another computer
they are still allowed. Only use a stick (or a copy of someone's game folder) you trust.

The game's own SETTINGS > GAME > STARTING AND LOADING has the same **PORTABLE MODE** row: there it asks the launcher
to move your data the next time the launcher starts (the launcher asks you first). Close the game before a move.

The folder is the switch: a folder called `appdata` beside the launcher makes the install portable, and deleting or
renaming it (with your data in it) makes it use this computer's user folder again. Two copies of the game each with
their own `appdata` folder are fully separate; a copy without one uses this computer's user folder. An update installed
with **Install update** keeps the `appdata` folder as it is.

On a write-protected stick, or one pulled out while playing, the game still runs but cannot keep your settings or
saves: the save failure is shown in the game (SAVE FAILED), and a copy of the save that could not be written goes to
`unsaved/` in this computer's user folder, so your progress is not lost. **Add to Steam** and the run logs name the
game's folder: after moving the game folder somewhere else, press **Add to Steam** again.

### Choosing a save file

On the launcher's **Saves** page, or in the game at SETTINGS > SAVES > SAVE FILE AT START, choose which file the game
uses. The change applies from the next start. More files mean more profiles. To take your progress to another
computer, copy the `profile-N.sav` file.

### Choosing a save folder (sharing saves between computers)

On the launcher's **Saves** page, **Choose save folder...** keeps your saves in a folder of your choice instead, for
example a OneDrive, Google Drive or Dropbox folder, or a folder on a NAS. Choose the same folder on each computer (a
PC and a Mac, say) and they play from the same saves. If the folder you choose has no saves yet, the launcher offers
to copy your current ones into it; it never deletes or replaces a save file, and the old folder keeps its files.
**Use the default folder** goes back. The choice applies from the next start and is kept in `launcher.ini`.

Play on one computer at a time, and let the folder finish syncing before you start the game on the other: the game
writes its save file each time it saves. If the chosen folder is not there when the game starts (a network drive not
connected, say), the game tells you and runs without saving.

### Importing your PS2 saves

You can bring your own progress over from a PS2 memory card or from PCSX2:

- a PS2 save exported as **`.psu`** or **`.max`** (the formats PS2 save tools make);
- a PCSX2 memory card file (**`.ps2`**), or a PCSX2 **folder** memory card. The card in PCSX2's first slot is
  usually **`Mcd001.ps2`**, in PCSX2's `memcards` folder: `Documents\PCSX2\memcards` on Windows,
  `~/.config/PCSX2/memcards` on Linux (`~/.var/app/net.pcsx2.PCSX2/config/PCSX2/memcards` for the Flatpak),
  `~/Library/Application Support/PCSX2/memcards` on a Mac. When that folder is there, the import starts in it;
- a `.sav` exported from this port.

On the launcher's **Saves** page, press **Import into file N...** (for a file), or **Import a folder card...** (for a
PCSX2 folder card). The launcher asks before it replaces the file in use, and keeps the old one as
`profile-N.sav.bak`. In the game, SETTINGS > SAVES > IMPORT SAVE does the same, and it applies at the next start.
**Export file N...** writes a copy of the file in use wherever you like.

### Backups

Every time the game saves, the file it replaces is kept first in `saves/backups/` as
`profile-N-<date-time>.sav`. The newest 8 copies of each file are kept, plus the first copy of each of the last 3 days; a save that fails to write never adds a copy. To go back to one, import it as above.

### When a save goes wrong

The game tells you about its saves in a small one-line bubble in the top right corner, for a few seconds: **SAVED** (green)
each time it has written your save file, and a red or amber bubble when something is wrong. It never shows the game's
own save screens for this.

- **SAVE FAILED - YOUR LAST SAVE IS KEPT - RETRYING**: the save folder did not take the save (a full disk, a folder
  gone offline). The game tries again every 10 seconds and says **SAVED AGAIN** once it works; your last good save
  stays as it was meanwhile.
- **NOT SAVED - CAN'T READ profile-N.sav**: the file is damaged, or holds a save the game itself would call corrupt.
  The game plays without saving in that session (it never writes over the file) and keeps a copy of it as
  `profile-N-unreadable-<date-time>.sav`. On the launcher's **Saves** page the file says **Can't be played**, and
  **Set aside file N and start fresh...** renames it to `profile-N-set-aside-<date-time>.sav` (kept, not deleted) so
  that the game starts the file empty. A damaged save is also refused when you import it.
- **NOT SAVED - profile-N.sav CHANGED OUTSIDE THE GAME**: the file changed on the disk while the game was running (an
  import with `save_tool`, another computer through a synced save folder). The game never writes over it: its saves
  for the rest of that session go beside it as `profile-N-unsaved-<date-time>.sav`. The same happens after IMPORT SAVE
  in the game, until the next start. The launcher's **Saves** page then shows that progress under the file: **Use it**
  makes it the file's save (the current one is kept as `profile-N.sav.bak` and in `backups/`), **Leave it** hides the
  line (the file stays).
- **SAVE WARNING - ANOTHER GAME ON THIS COMPUTER USES THIS SAVE FOLDER**: two games are running from the same save
  folder; play one at a time. The game marks the folder it plays from with a small `game.lock` file, removed when it
  closes; one left behind by a crash, or one from another computer sharing the folder, is ignored. The launcher's Set
  aside waits until the game is closed.

The port never deletes the `-unreadable-`, `-set-aside-` and `-unsaved-` files, and never reads them as profiles:
delete them yourself when you no longer need them.

## 7. Settings worth knowing

The port's settings are on the **SETTINGS** page. You can reach it from the game's main menu (the row above QUIT
GAME), from the pause menu during a race, or from the launcher's **Settings** page. Changes apply at once, unless the
row says "next start". The game's own options (volumes, the DJ, the controller layout, vibration) stay where the game
keeps them, in **DRIVER DETAILS > SETTINGS**.

**With every setting at its default, the game plays exactly like the PS2 original.** Nothing here changes physics,
AI or gameplay, except the few settings marked **opt-in** below. Those are off until you turn them on.

### Picture and speed

- **QUALITY PRESET** (GRAPHICS, the first row): PS2 ORIGINAL, LOW, MEDIUM, HIGH (the default) or ULTRA. This sets every quality and
  effects row at once.
- **BLOOM** (EFFECTS): Off, Soft (the default) or Standard. Standard is the game's own glow around bright lights, which
  is strong on bright tracks (a white sky or a lit sign can wash out). Soft keeps the same glow at about 60% of its
  strength and blends it in so the highlights never clip into white blobs. The PS2 ORIGINAL preset uses Standard.
- **SPARKS** and **CRASH DEBRIS** (EFFECTS): Off, Low, Medium or High (the default). High draws all of them, as the
  game does. Medium draws half, and Low a quarter (a little larger, so a crash still sparks brightly). A crash with
  thousands of sparks is the heaviest picture the game draws, so these are the rows to lower on a weak PC, a handheld
  or a laptop. It is drawn only: the crash itself is the same. The same particles stay on screen from one frame to the
  next, so nothing flickers. The QUALITY PRESET sets them: LOW is Low, MEDIUM is Medium, the others High.
- **RENDER SCALE** (GRAPHICS): 200%, 150%, 125%, 100% (the default), 85%, 75%, 67% or 50%. Below 100% the 3D picture is
  drawn at that share of the window and scaled up, which a weak graphics chip draws faster; the HUD and menus stay sharp.
  LOW uses 75%. Above 100% it is drawn larger and scaled down (supersampling): smoother edges, less shimmer on thin wires
  and fences, at a large cost for the graphics card (200% draws four times the pixels); 150% and 200% want a card with
  plenty of memory. In a very large window it is held to what the graphics card can take.
- **BATTERY SAVER** (DISPLAY > WINDOW, off by default): while the PC runs on battery, the frame rate is held to 60 and the
  sparks and crash debris are drawn at Medium at most. Plugged in again, your settings are back. Nothing is changed in
  the game or your saved settings.
- **For a weak PC, a Steam Deck or a laptop on battery:** choose the LOW preset (no anti-aliasing, no full-screen blur
  or bloom, a quarter of the sparks, the 3D picture at 75%), a FRAME RATE LIMIT of 60 (or 30), and a smaller window. If
  it is still slow, lower RENDER SCALE or set DRAW DISTANCE to PS2.
- **FRAME RATE LIMIT** (DISPLAY): Unlimited (the default), 30, 60, 120, 144, 165 or 240. The game itself always runs
  at its own speed. **FRAME INTERPOLATION** draws smooth frames in between the game's own; turn it off to see the
  PS2's 60 / 30 steps. **VSYNC** is off by default.
- **FULLSCREEN** (DISPLAY): borderless fullscreen. F11 also switches it.
- **FAST LOADING** (GAME > STARTING AND LOADING, on by default): loading screens go as fast as your PC can. The game is the same; only
  the wait is shorter.
- **ANTI-ALIASING** up to MSAA 8x and **TEXTURE FILTERING** up to anisotropic 16x (GRAPHICS > IMAGE), **CAR DETAIL**
  (CAR > CAR LOOK), shadows, lights and the other effects (GRAPHICS and EFFECTS).
- **CAR REFLECTIONS** (CAR > CAR LOOK): Enhanced (the default) gives the paint and the glass of the cars in a race (yours, the
  other racers, and a little on traffic) a glossy reflection of the track's sky, the horizon and the sun, strongest at
  the edges of the body. In both Enhanced and Original the cars mirror the track around them as on the PS2: in a tunnel
  you see the tunnel and its lights, not the sky. Original is the game's own faint reflection, and Off draws none. The QUALITY PRESET PS2
  ORIGINAL and LOW use Original. It is drawn only: the game itself is unchanged.
- **CAR PAINT** (the CAR tab): Custom draws your car in your own colour (a ready **COLOUR**, or PAINT RED / GREEN / BLUE,
  with a swatch of the colour beside them), DECALS Off takes its logos, numbers and stripes away, and **FINISH** gives both
  players' paint a Metallic look (a stronger reflection in the paint's colour, with flakes that sparkle in the sun) or a
  Pearl one (the colour shifts as the car turns). **PLAYER 2 CAR PAINT** has the same rows for player 2's car in the
  split screen. All of it is drawn only: the game, replays and saves keep the paint picked in the car select. The
  launcher's Showroom shows your custom paint and finish.

- **Night mode** (TIME OF DAY) is still being finished and is not in this version: it comes in a later update. The
  TIME OF DAY PER RACE shortcut in CONTROLS > BINDINGS belongs to it and does nothing yet.
- **SURFACE DETAIL** (GRAPHICS, section LIGHTING, off by default): relief and a sheen on the road, kerbs and walls from
  their textures, lit by the sun. (AMBIENT OCCLUSION is held back in this version while it is being fixed.)
- **DRAW DISTANCE** (GRAPHICS): Whole track (the default) draws the whole course with no pop-in in the distance, and
  PS2 keeps the original's view.
- **FPS COUNTER** (GAME > INTERFACE): a frame-rate readout in a corner (**FPS COUNTER POSITION** picks which; top left by
  default). Minimal is a small plate with just the number. Standard adds the 1 % low under it (the rate of your slowest 1 %
  of frames over the last 8 seconds). Both stay small, at most about 8 % of the window's width, so they cover little of
  a menu's title. Detailed is the big plate (about 12 % of the window's width at most on a large window): it adds a graph of frame times over the last 8 seconds with a dashed line at
  the target (the frame rate limit's frame time or 60 Hz's), and shows the average, the current and the worst frame time, the latency, the resolution, anti-aliasing and
  quality preset, and your graphics card. The number is green at the target, amber below it and red far below it, and
  its three small cells say the same without colour. **F3** switches between the views (and off) while you play. It is
  never in a screenshot, and in photo mode it sits at the top left.
- **PHOTO MODE IN PAUSE MENU** (GAME > INTERFACE): On (the default) adds a PHOTO MODE row to a race's pause menu, after
  SETTINGS. Off leaves the pause menu as the game made it; P and L3 + R3 still open photo mode.
- **SPEED UNITS** (GAME > LANGUAGE AND UNITS): MPH (the default, as the US game) or KM/H. The speedometer, the distance messages,
  records and stats then use kilometres and metres. The speedometer changes from the next race.

### Widescreen and field of view

- The port fills any window shape, ultrawide included. **RACE HUD LAYOUT** (DISPLAY) puts the race HUD at the screen's
  edges (Wide, the default) or keeps the PS2's 4:3 layout. **MENU LAYOUT** chooses how the menus fit a wide screen.
  **MENU MOVIE SIDES** fills the sides of the 4:3 menu videos with black or with a blurred copy of the video.
- **FIELD OF VIEW** (DISPLAY > CAMERA): from 60 to 120, where 90 is the game's own. **FOV CORRECTION** keeps your car the same
  size on screen.

### Photo mode

Pause a race (also during a crash or a Takedown cam), then choose **PHOTO MODE** in the pause menu (it follows
SETTINGS), or press **P**, or **L3 + R3** on a gamepad. The HUD goes away and you get a free camera. Photo mode is for
one-player races: in the two-player split screen there is no row, and P and L3 + R3 do nothing. The **PHOTO MODE IN
PAUSE MENU** setting (GAME > INTERFACE, on by default) can hide the row; P and L3 + R3 work either way:

- **Keyboard:** WASD move, E / Q up / down, Shift fast, Alt slow, the mouse or the arrow keys look, the mouse wheel
  or the `-` / `=` keys zoom, Z / C tilt, R back to the game's view, **F12 takes the picture**.
  The mouse looks and zooms, and Escape also leaves; every other key and every gamepad control here can be changed in
  CONTROLS > Shortcuts (the controls strip at the bottom of the screen shows what you set).
- **Gamepad:** left stick move, right stick look, RT / LT up / down, LB / RB tilt, D-pad up / down zoom.
- **Depth of field:** F (gamepad: Back) turns it on. V / B (Start) sets the blur, N / M (D-pad left / right) moves
  the focus, and G (L3) chooses between focusing on your car or on the screen centre.
- **Motion blur:** J (gamepad: a quick click of R3 on its own) cycles Off, Tracking, Panning and Static. K / L (R3 + D-pad left /
  right) set the shutter from 1/500 to 1/15 s (1/60 at first). The streak is the speed times the shutter, as in a real
  photo, so a slow car hardly blurs. Tracking: the camera travels with your car (it stays sharp, nearby things streak
  most, the far skyline stays sharp). Panning: the camera stands and turns to follow your car (it stays sharp, the
  background streaks); this looks best from the side. Static: the camera holds still (the scenery is sharp, the cars
  streak). Cars move as fast as they were going when you paused. The picture builds up over about a quarter of a
  second; F12 always saves the finished one.
- Zoom, tilt and focus speed up the longer you hold the key; Shift (gamepad: A / Cross) makes them faster, Alt finer.
- P, Escape, or B / Circle leaves photo mode, and the race stays paused.
- **On-screen display:** a plate at the top right shows the mode and live gauges (field of view, tilt, depth of field
  and its focus, motion blur and shutter); a strip at the bottom lists the controls as key caps, or as the buttons of the pad in use (the
  BUTTON PROMPTS setting). **H** (gamepad: Y / Triangle) cycles it: everything, the gauges alone, nothing. It scales
  with HUD SIZE, sits at the screen edges at every aspect ratio, and is never part of a screenshot.

### Accessibility (GAME > ACCESSIBILITY)

- **SCREEN FLASHES Reduced** tones down full-screen white flashes and the flicker of camera changes.
- **HUD COLOUR FILTER**: Deuteranopia, Protanopia or Tritanopia. This shifts the HUD's and menus' colours for
  colour-blind players. The race itself is not changed.
- **HUD SIZE** 115 %, 130 % or 150 % draws the race HUD larger (position, lap, score, boost, speed and the race's
  messages), each part growing from its own corner. The menus keep their size.
- **BOOST Toggle** (*opt-in*): press boost once to keep boosting, and again to stop. This changes the input the game
  gets, so Hold, the original behaviour, is the default.

### Music (AUDIO > MUSIC, or the launcher's Music page)

Choose a **MUSIC FOLDER** of MP3, FLAC, Ogg Vorbis or WAV files (subfolders count too), then the **PLAYLIST**: EA
Trax only (the original and the default), Custom only, or Mixed. Your songs play through the game's own radio, with
its volume, shuffle and the EA TRAX popup showing each song's title, artist and album. Your songs are also listed
after the game's 44 on the game's own EA TRAX page (DRIVER DETAILS > SETTINGS > EA GAMES TRAX; the first 200 of the
folder), where each one is set to ALL, RACE ONLY, MENU ONLY or OFF like the game's songs; Custom only plays
only yours. Changes apply from the next song. **CUSTOM MUSIC VOLUME** (0% to 200% in steps of 10, default 100%) sets
how loud your songs play, on top of the game's RADIO VOLUME: at 100% a song plays exactly as loud as the game plays its
own songs, so a quiet file (many downloads are mastered or normalised quieter than the game's songs) can be raised to
match, up to twice as loud. A limiter keeps loud songs from clipping; the game's songs and the DJ do not change. It
applies at once. **OTHER MUSIC APPS** (Windows and Linux, off by default) shows the song Spotify, a browser or any other player is
playing in the same popup; that music still plays from its own app, so turn the game's RADIO VOLUME down to hear only it.

### Opt-in features (off by default)

| Setting | What it does |
|---|---|
| **RUMBLE** (CONTROLS > FEEDBACK) | The game's own vibration on your gamepads, for both players, with **RUMBLE STRENGTH** and **TRIGGER RUMBLE** (the trigger motors of Xbox One and Series pads). The game's VIBRATION option in Driver Details still applies. |
| **GHOST: Best lap** (GAME > MODES) | In Time Attack, your best lap on the track races with you as a see-through car. It is only drawn: the other cars and the game never know it is there. |
| **INSTANT RESTART** (CONTROLS > DRIVING) | F5, or Select + Start on a gamepad, restarts the event while you drive. It goes through the game's own pause menu (RESTART, YES), so the game gets menu presses you did not make. |
| **BOOST Toggle** (CONTROLS > DRIVING) | See Accessibility above. |
| **DEVELOPER MODS** (MODS > DEVELOPER) | A tuning console for the game's own values (a DEVELOPER tab and F9 in a race). Edits change gameplay. Meant for mod makers. Its CHANGED tab lists what you changed; R (Square) puts a value back, Ctrl+R (Select) puts back everything after asking. What each value does: [tuning options](modding/tuning-options.md). |

These are off by default too, but they do not change the game:

- **SPEEDRUN TIMER** shows the event's time in game time, with your bests and lap splits.
- **RECORD REPLAYS** keeps each session as a small replay file that plays back exactly (the launcher's Replays page).
  Watching a replay never changes your own ghosts or best times.
  The newest 20 are kept.

## 8. Mods

Mods change what the game loads from your disc without touching the disc. With no mods on, you play the game exactly
as it is on your disc.

**Installing a mod:**

1. In the launcher, open the **Mods** page.
2. Press **Add mod...** and choose the mod's **`.b3mod`** or **`.zip`** file. The launcher checks it and unpacks it
   into its own folder. A damaged or unsafe archive is refused, and nothing is written.
   - A mod that comes as a **folder**: press **Add folder...** and choose it (it is copied into the mods folder), or
     press **Open mods folder** and copy the folder in yourself, so that `mods/<the mod's folder>/mod.ini` exists,
     then press **Look again**.
   - A **pack of replacement files without a `mod.ini`** (a folder or zip holding the game's own folders, such as
     `PVEH`, `TRACKS` or `DATA`): the launcher asks whether to install it as a file-replacement mod named after the
     folder or zip, and writes its `mod.ini` for you. Such a mod has no code, so nothing in it runs. Anything else
     without a `mod.ini` is refused, with a note on what a `mod.ini` is.
3. Press **Turn on** on the mod's card. New mods are off.
4. If you use several mods, order them with **Move up / Move down**. When two mods change the same file, the one
   higher in the list wins, and the page shows these conflicts.
5. Start the game. The changes apply from the next start.

**Mods that contain code.** Some mods carry a program library ("Contains code" on their card). When you turn one on,
the launcher asks: **"Allow this mod's code to run?"** Such code runs as a program on your PC with your user's rights,
like any program you install. **Allow code only from people you trust**, ideally with its source. You are asked again
when any of its code changes (a new version of the mod, or a library inside it). Lua script mods run inside the game
and can only do what the script interface offers, so they do not need this permission, except a script that says it is
unsafe (its card says "Unsafe script": it may write the game's memory and call its functions), which is asked about in
the same way.

A card that says **"Changes gameplay"** marks a mod whose times and replays are kept apart from normal play: while
such a mod is on, your ghosts and best times go to folders of that mod set (`ghosts/modded-<set>/`,
`speedrun/modded-<set>/`, named by the gameplay-changing mods only, so turning a texture or sound mod on or off
keeps them), and normal play never sees them; a replay made with it plays only with the same mods. Safe mode
(section 9) always starts without mods.

Want to make mods? Start with the [modding guide](modding/README.md).

## 9. Troubleshooting and bug reports

### Safe mode

If the game crashes or does not start, the launcher's Play page says so the next time you open it.

- **START IN SAFE MODE** starts the game once with low settings: a window, the LOW preset, no anti-aliasing, no HD
  textures, vsync, and no mods. It also turns off the guest memory guards (see "guest memory overrun" under Common
  problems). Your own settings file stays as it is.
- After **two starts in a row that did not end cleanly**, the game picks safe mode by itself. The window title says
  SAFE MODE. This automatic safe mode keeps the guest memory guards on, unless the run before it ended with a "guest
  memory overrun". Once a run ends cleanly, the next start uses your own settings and mods again. On the Play page,
  **Clear the crash count** stops safe mode from being chosen for you.

### Logs

Every game run leaves a log and a recording of your controls in the `logs` folder of your user data folder (section
6): `run-<date-time>.log` and `run-<date-time>.pads.txt`. The last 10 runs are kept. With the recording, a developer
can replay exactly what happened.

On Linux you can also start `./b3launcher` or `./native_play` from a terminal to see their messages directly. On a
Mac, the same from Terminal: `"/Applications/Burnout 3 Native.app/Contents/MacOS/b3launcher"`.

### Sending a bug report

1. Note what happened and roughly when (which mode, track or menu).
2. Make a report:
   - **in the game, press F10**: a banner says "BUG REPORT SAVED" and where it went;
   - **or in the launcher**: Play page, **Bug report...**, then **Make report** (or **With my save**). Its folder
     opens afterwards.
3. You get one file, `bug-report-<date-time>.zip`, on your Desktop (or in Documents; on Linux, in your user data
   folder if you have neither). Attach it to a [bug report](../../../issues/new/choose) with your note.

If the game crashed, just make the report afterwards: the crashed run's log is in it.

The zip holds the logs and controls recordings of your last 5 runs, your settings files, the port's version, and a
short description of your system (the OS, CPU, memory, graphics card and driver, and display). It holds **no game
data** and **no save file**, unless you choose **With my save** when you make it on the launcher's Play page (which helps
when the problem depends on your progress). Your user name is taken out of the folder paths in it (they read `C:\Users\<user>\...` or `~/...`). Nothing
is ever sent anywhere by itself: you send the report yourself.

### Common problems

| Problem | What to do |
|---|---|
| **The game window closes at once, or there is no picture.** | Most often, the graphics driver has no Vulkan. **Windows:** install the newest driver from your graphics card maker's own website. **Linux:** check with `vulkaninfo --summary` (from the `vulkan-tools` package). To install the loader and the open drivers, use `sudo apt install libvulkan1 mesa-vulkan-drivers` (Ubuntu, Debian, Mint), `sudo dnf install vulkan-loader mesa-vulkan-drivers` (Fedora), or `sudo pacman -S vulkan-icd-loader vulkan-radeon` (Arch; `vulkan-intel` for Intel, `nvidia-utils` for NVIDIA). NVIDIA cards need NVIDIA's own driver: the open "nouveau" driver is too slow or has no Vulkan. Then make a bug report from the launcher. |
| **"SLUS_210.50 is not the US Burnout 3: Takedown program"** | Your disc is another region or version (or the copy is damaged). The port runs only the US disc, SLUS-21050. |
| **"Not the game's folder: no ..." or "Not a disc image"** | The launcher did not find the game's files where you pointed it. Choose the folder that directly holds `SLUS_210.50`, `DATA`, `TRACKS` and the rest, or the `.iso` file itself. If the game is one folder further down, the launcher says where. |
| **"The disc image is incomplete"** | The image was cut short. Make it again (section 2). |
| **Windows: "The renderer ... b3render.dll was found, but Windows could not start it (error 1114)"** | Another program's DLL failed to start inside the game: most often an overlay, recorder, performance monitor or security tool that puts itself into games, or a `.dll` file added to the game folder. Close such programs (or exclude `native_play.exe` from them), keep only the files from the download in the game folder, and try again. The run log (Logs, above) lists the DLLs involved: attach the bug report. |
| **START GAME is greyed out** | Either the game data is not set (Game Data page), or the game program is not beside the launcher. Keep all the files from the download in one folder. |
| **Linux: `version 'GLIBC_2.35' not found`** | Your distribution is older than the build supports (section 1). |
| **Linux: "Permission denied"** | The archive was unpacked without the programs' execute permission: run `chmod +x b3launcher native_play add-to-menu.sh` in the folder. |
| **Linux: the controller also clicks or types on the desktop (windows open while you play)** | On Linux a game cannot take a controller for itself: other programs see the same presses. Most often it is Steam: with Steam running, its **desktop layout** turns the controller into a mouse and keyboard for everything that Steam does not know as a game. Start the port from Steam (add `b3launcher` as a non-Steam game, or use **Add to Steam** on the launcher's Settings page), or turn the desktop layout off in Steam's Controller settings, or close Steam while you play. Other controller-to-keyboard tools (AntiMicroX and the like) do the same. |
| **Linux: no file picker opens** | Install `xdg-desktop-portal` (with your desktop's portal) or `zenity`. You can also use **Type a path...** in the launcher. |
| **The game closes and the log or crash report says "guest memory overrun"** | The game tried to read or write past the end of the console's memory, and the port stopped it there before it could do any harm. Please send a bug report from the launcher (Make a bug report on the Play page): it has what we need to fix it. Until a fix is out, **START IN SAFE MODE** runs without this check (so does the safe mode the game picks by itself after such a crash), or set the environment variable `B3_GUEST_GUARDS=0` to turn it off with your own settings. The game may then still crash or misbehave at that point. |
| **The race falls behind or the sound stutters** | Lower the QUALITY PRESET, set a FRAME RATE LIMIT, or try safe mode. If that helps, please send a bug report: it tells us what your PC needed. |

## 10. Known issues

These are the problems we know about in the current build. Please report them anyway when you run into them, with a
bug report.

- **Online play does not work yet.** Picking ONLINE on the main menu shows a message box that reads "Online play is
  coming soon."; OK takes you back. The original servers are long gone, and online play is coming in a future release.
- **Distant traffic** can still visibly appear on the road ahead (it now fades in over a short moment).
- **Some thin menu text** (for example the takedown checklist under Driver Details > Trophies) can be hard to read at
  some window sizes.
- **The game's own profile list holds three profiles per save file.** Use more save files (section 6) for more
  profiles.
- **A few button prompts** (the Aftertouch hint, and button names written inside the game's text) still show the
  PlayStation buttons whichever pad you use.
- **The Crashbreaker explosion** is drawn in part: its brief full-screen orange flash is (SCREEN FLASHES Reduced limits it),
  So are the smoke, dust, glass and fire particles (at a lighter share than the SPARKS and CRASH DEBRIS levels: High draws half of them). The flying debris pieces of a crash (the game's debris array) are
  not drawn yet.
- **Replays:** a session that ended in a crash of the game (or was killed) is kept up to its last 10 seconds: the
  replay is saved every 10 seconds of play while it records. The Replays list shows the date and length, but not the track. A replay made with your own music plays back only with the same music settings and
  folder.
- **Mac:** the Mac app has been played less than the Windows build, and photo mode's depth of field has never run on
  a Mac (if it fails, it simply turns itself off). The app is not notarized, so its first start needs **Open Anyway**
  (section 3). If the frame rate in the game is low, try SETTINGS > DISPLAY > FRAMES IN
  FLIGHT 2 (it applies at the next start).
- **Linux:** the package has passed its automatic tests but has not yet been played on a range of real distributions
  and the Steam Deck. Reports from Linux players are very welcome.
