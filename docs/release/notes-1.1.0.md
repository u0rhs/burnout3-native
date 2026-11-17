# Burnout 3 native 1.1.0 (draft release notes)

Status: 🚧 built, being checked. Nothing here is released yet.

## New

- **Languages.** Own a European copy too? The launcher's **Settings > INTERFACE > IMPORT LANGUAGES** copies its
  languages once (French, German, Italian, Spanish, Dutch, Swedish or UK English, whichever your disc has), and
  **LANGUAGE** switches the game's text to one of them. No game text ships with the port: it comes from your disc.
- **The launcher and PC SETTINGS in your language.** The launcher and the in-game PC SETTINGS page follow the same
  choice, in our own translations (French, German, Italian, Spanish, Dutch, Swedish). English stays the default.
- **Analog triggers.** A trigger on accelerate or brake now works gradually, like a pedal, through the game's own
  analog throttle and brake. Both triggers together still powerslide. On by default (Settings > INTERFACE >
  CONTROLLER > ANALOG TRIGGERS).
- **Chase camera distance and height.** CAMERA tab: move the race camera back (up to +50%) and up (up to +1 m); it
  tilts to keep your car in view. Drawn only: the game is unchanged.
- **Sharp upscaling.** GRAPHICS > UPSCALING: with a RENDER SCALE below 100% (weaker GPUs, handhelds), a sharper picture
  than plain bilinear scaling.
- **Your own save folder.** The launcher's Saves page can keep your saves in a folder you choose (a Google Drive,
  Dropbox or OneDrive folder, or a NAS), so two computers can share them. It offers to copy your saves there and never
  replaces a file.
- **Easier PCSX2 imports.** Importing a save starts in PCSX2's memory-card folder and points at the card
  (usually `Mcd001.ps2`).
- **Steam Deck.** The launcher's first start on a Deck picks the MEDIUM preset with vsync on, to hold 60 fps there.

## Fixed

- FRAME RATE LIMIT (and BATTERY SAVER's 60) keeps a steady schedule, and leaves the pacing to vsync when vsync already
  holds that rate (a 60 limit on a 60 Hz screen could show 55-58).
- With a language pack, the ONLINE dialog shows "Online play is coming soon." in that language.

## Notes

- The README has a new FAQ: how this port differs from other PC versions, what PC you need, AI assistance, discs and
  saves.
- Requirements are unchanged: Windows and Linux need a GPU with a Vulkan 1.0 driver; macOS runs on Metal (Apple
  Silicon). You still need your own US disc (SLUS-21050).
- Saves and settings from 1.0.0 carry over.
