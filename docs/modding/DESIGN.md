# Mod kit: design

The plan for the port's mod kit (2026-11-12: a set of modding tools and documentation, so the community has full
modding freedom). One plan, so each part is built to the same rules. Part 1 (the mod loader
core) is built (modkit1, 2026-11-12), with its Phase 1b (modkit1b: the renderer's readers, archives, conflicts,
requirements); Phase 2a of part 2 (the hook table and the mod arena, hooks2a) and Phase 2b (the code-mod SDK, sdk2b);
part 3a (b3modtool: list, extract, txd, strings, diff / apply, pack, verify, roundtrip; modtool1); of part 3b the vdb
and audio commands (modtool3b); the data side's last two pieces, `[files] patches` applied at run time and
`[textures]` packs per mod (patches3); part 4, the tuning console with `[tuning]` presets (tune4, §4.1); and Phase 6,
Lua scripting (lua6, §10.1). The other parts are design.

Tags as elsewhere: **[C]** confirmed (code, a run, the decomp), **[L]** likely, **[G]** guess.

## 0. Ground rules

**The game is native C++.** native_play runs Criterion's code as C++ generated from the decomp (gen/native), not the
disc's program. A patch to `SLUS_210.50` (a PS2 cheat, a pnach, a patched ELF in an xdelta) never runs here: native_play
accepts only the retail ELF (size + FNV-1a 64) and takes nothing but its data image from it. So there are two kinds of
mod, and the kit has a tool for each:

| Kind | What it changes | How the port applies it |
|---|---|---|
| **Data mod** | the game's files: textures, strings, models, tracks, sounds, movies, tuning tables | the loader's **overlay** in the port's file layer (§1): the game opens a disc path, a mod's file answers |
| **Code mod** | what the game's functions do | a shared library loaded by native_play through a small versioned C ABI (§2), hooking functions **by their retail guest address** on the native table |

A mod may be both (a code mod that reads its own data files). A mod that needs new behaviour from the PS2 code it was
made for re-implements that behaviour natively through §2; there is no way to "run the patched PS2 code".

**Principles** (each one is an acceptance item in §9):

1. **No mods enabled = the game byte for byte.** No overlay object, no hooks, no extra guest writes: the
   golden checks (`tools/golden/compare.sh`) PASS unchanged. Offscreen runs (tests, golden checks) never read the
   user's mod settings; they run mods only when a script names them (`--mods`).
2. **Mods are opt-in and visible.** Nothing is on until the user turns it on (launcher Mods page). Every run prints the
   active set and its hash; bug reports, pad recordings (replays) and ghosts carry it.
3. **Gameplay-changing mods are opt-in by nature** and marked as such (§6); the port's own "default never changes
   gameplay" rule (CONTRIBUTING.md) is about the port's features, a mod is the player's choice.
4. **Never modify the user's disc.** The overlay reads; nothing is written to the disc folder, the image or a mod's
   folder at run time.
5. **Mods carry their own content**, never the game's files wholesale (§8); the tools work on the user's own disc.

## 1. Mod loader, data overlay, manifest, load order, launcher page

### 1.1 As built (Phase 1, modkit1)

Code: `include/b3mod/mods.h`, `src/b3mod/mods.cpp` (standard library only), the overlay hook in
`src/native/platform_disc.cpp` (`disc::set_overlay`), native_play's options (`tools/native/native_play.cpp`), the
launcher's Mods page (`src/b3launcher/main.cpp` page_mods), tests `tests/test_mods.cpp`, the functional check
`tools/modding/text_mod_check.sh`. User guide: [getting-started.md](getting-started.md).

**Folders.** `<user data folder>` is the game's prefs folder (`SDL_GetPrefPath("b3port", "native")`: beside
pc_settings.ini; Windows `%APPDATA%\b3port\native`, Linux `~/.local/share/b3port/native`, macOS
`~/Library/Application Support/b3port/native`).

```
<user data folder>/
    mods.ini                      the load order and on / off (the launcher writes it)
    mods/<any folder name>/
        mod.ini                   the manifest
        data/                     the overlay root ([files] root): disc paths below it, any case
            DATA/FRONTEND.TXD
            TRACKS/US/C2_V2/STATIC.DAT
        README.txt, LICENSE ...   anything else: ignored by the loader
```

**The manifest** (`mod.ini`, a flat ini: `[section]`, `key = value`, `#` / `;` comments, a value in double quotes keeps
its spaces; our own ~60-line parser, no dependency):

| Key | Meaning |
|---|---|
| `[mod] id` | required; `a-z 0-9 . _ -`, 1..64, not starting with `.`; the name in mods.ini, `--mods`, logs. Two folders with one id: the second is refused |
| `name`, `author`, `description` | shown on the Mods page (`name` defaults to the id) |
| `version` | any text without spaces (default `0`, with a note); part of the set hash |
| `api` | the mod API version the mod was made for. Missing: read as 1, with a note. Newer than the port's (`b3mod::kApiVersion`, now 1): refused ("a newer port is needed") |
| `priority` | an integer (default 0): where a newly installed mod joins the load order (higher: nearer the top) |
| `[files] root` | the overlay folder inside the mod, default `data`. Must be relative and stay inside the mod (`..`, absolute paths, drive letters, a link that leads out: refused) |
| `[requires]` | Phase 1b: other mods this one needs, `id = least version` (§1.2) |
| `[tuning] preset` | part 4 (tune4, §4.1): a tuning preset file inside the mod; the mod then changes gameplay |
| other sections | reserved for later parts: a note "not used by this port", never an error (`[code]` §2 is read since sdk2b, `[textures]` §1.3 since patches3, `[script]` §10.1 since lua6) |

**Load order.** `mods.ini`:

```ini
[order]
hello-texture = on     # first line: the highest priority
other-mod = off
```

The first enabled mod in the list that has a path supplies it. The launcher merges the file with what is installed: a
removed mod drops out, a new one joins at the end (higher `priority` first), off.

**The overlay** (`b3mod::Overlay`, built once at start, immutable during the run):
- Every file under each active mod's root, keyed like the game's paths: device (`cdrom0:`) and `;1` dropped, `\` or
  `/`, ASCII upper case. `data/data/frontend.txd` and `data/DATA/FRONTEND.TXD` are the same key.
- `platform::disc::resolve` (the one place both disc readers go through: the GTFS drive rows, `platform_gtfs.cpp`,
  and RW Audio's IOP file reads, `platform_rwa.cpp`) asks the overlay first. A hit opens the mod's file: its size is
  what Open reports, its bytes what Read returns (bytes past the end read as 0, as the disc's do). Existence is
  consistent: a path a mod supplies exists, even if the disc lacks it; there is no directory listing in the game's
  file system (it opens by name [C platform_gtfs.cpp]), so nothing else needs merging.
- Refused, each with a note in the log: a request with `.` / `..` components, another device or a drive letter; in a
  mod, two files whose names differ only in case, a file reached through a link out of the mod, files over 4 GB,
  `SLUS_210.50` and `SYSTEM.CNF` (the program never runs from a file: code is §2), and the file managers' own files
  (`.DS_Store`, `Thumbs.db`, `desktop.ini`, any dot file: so a mod hashes the same on every system).
- The first time a mod supplies a path the log says so: `mods: DATA/GLOBALUS.BIN from proof-text (214850 bytes)`.

**The set hash** (recomputable by any tool; FNV-1a 64 over text, as the port's other identities):
- per mod: over `file <KEY> <size> <fnv64 of the bytes, 16 hex>\n` for each file, by key;
- the set: over `b3mods api 1\n` then `mod <id> <version> <mod hash>\n` per active mod, in priority order.
So the hash names exactly the bytes the game could read, in the order that decides who wins; the folder's place on disk
does not matter. (Hashing reads every file once per start: ~1 s per GB. A cache keyed by path, size and mtime in
`<user data>/mods/.hashcache` is a later improvement; the hash itself stays the same.)

**native_play.** `--mods a,b` (or `B3NATIVE_MODS`): these mods, first wins; `none`: no mods. An id that is not
installed or not usable ends the run (exit 2): a script never gets half a set. Without `--mods`, a window run takes
mods.ini's enabled list (a missing or broken mod there is skipped with a message); an offscreen run takes none.
`--mods-dir DIR` (`B3NATIVE_MODS_DIR`) names the mods folder. At the start the log has

```
native_play: mods: proof-text 1 ("Proof: one menu label") from <dir>: 1 files, mod hash 1f0e...
native_play: mods: active (--mods; the first wins): proof-text 1 (1 file); set hash 9c4d...
```

or `native_play: mods: none`. The run log (`--log`, the launcher's default) holds these lines, so the bug report's
`logs/` carries them; the bug report also takes `mods.ini` (never a mod's files). A pad recording gets
`mods <set hash> <ids>` after its header (no line without mods: recordings without mods are unchanged); a replay whose
recording names another set says so (`the recording was made with mod set X, this run has Y`). race_check.sh takes
`MODS=` / `MODS_DIR=`.

**Launcher Mods page.** Lists the installed mods in load order, one card each: number, name, ON / OFF / CANNOT BE USED,
id, version, api, author, folder, description, the manifest's errors (red) and notes (amber, e.g. "no api version");
Turn on / Turn off, Move up / Move down (each change saved to mods.ini at once); Open mods folder (made if missing),
Look again. The Play page shows a "Mods" status line ("None on: the game as on your disc" / "2 on: a, b").

### 1.2 As built (Phase 1b, modkit1b)

Code: `include/b3mod/mods.h` (requirements, conflicts, `renderer_args`), `include/b3mod/archive.h` +
`src/b3mod/archive.cpp` (the zip reader and the installer; zlib), the disc layer's overlay in
`include/b3asset/discfs.h` (`set_overlay`), the renderer's arguments in `src/b3replay/main.cpp`, native_play's safe-mode
rule, the launcher's Mods page (Add mod, conflicts, requirements) and its art (`src/b3launcher/gameart.cpp`). Tests:
`tests/test_mods.cpp` (20 cases); the functional check `tools/modding/track_mod_check.sh`. Every key and file:
[mod-format.md](mod-format.md).

- **The renderer's own disc readers.** b3render reads disc files itself: the route lookup by the STATIC.DAT header
  (`capture.cpp route_by_header`), the whole-track and draw-distance loads (`view.cpp find_route`, the route's
  STATIC / STREAMED / ENVIRO.DAT, GAMEDATA.BGD), the car scan (`scan_cars`), DATA/GLOBAL.TXD for effects and the
  front end's scenes (src/b3view). All of them go through `b3asset::disc` (discfs.h), so the overlay sits there:
  `disc::set_overlay(root, files)` takes the winning file of each key, and every query for a path below `root`
  (lexically, after `lexically_normal`; any case) sees the mods' files first: `read_file`, `File::open`, `file_size`,
  `is_file`, `exists`; `is_dir` is true for a folder that holds a mod file, `list_dir` adds the names only a mod has
  (a new route folder is found by `find_route`'s walk), `resolve_ci` finds them. Paths outside `root` (the HD pack,
  ghosts, settings) are never touched, and with no overlay set nothing changes.
  The set reaches the renderer over the existing native_play -> renderer interface, the renderer's argument string
  (`b3gv_native::Options::args`, B3PORT_RENDER_ARGS): native_play appends `b3mod::renderer_args(mods folder,
  overlay)` = `--mods-dir "<dir>" --mods <ids> --mods-hash <set hash>`. The ids are native_play's final active set
  (after `[requires]`), so there is one decision; the renderer only rebuilds the same `b3mod::Overlay` from them,
  compares its set hash (a mismatch -- a mod's file changed between the two reads -- is a WARNING line, and the
  renderer goes on with what it read) and calls `set_overlay(--game, overlay.winners())` before any reader runs. The
  log has `[b3render] mods: <set> over <game> (<n> files)`. No mods: no arguments, no overlay.
  The launcher's art (`load_game_art`: GLOBAL.TXD, FRONTEND.TXD) takes the mods that are on in mods.ini, through
  `Overlay::find`; it is read again when the set changes. Its game-folder check reads the disc alone (it checks the
  disc).
  Proof: `track_mod_check.sh` makes a test mod (under build/ only) from a copy of the route's STATIC.DAT the race
  check drives, with the largest paletted texture's CLUT set to magenta; the modded run's race shots change, and only
  towards magenta (difference masks), while the game reads the same file (`mods: TRACKS/.../STATIC.DAT from
  proof-track`) and the renderer reports the same set hash. [C 2026-11-12: US/C2_V2, the pavement texture
  Chgo_Pavement_02: 2.75 % and 0.06 % of the two race shots changed, every changed pixel towards magenta, on the
  pavement only; with no mods the golden compare is unchanged.]
- **Install from an archive.** "Add mod..." on the Mods page (an SDL file dialog; a typed path where there is none)
  calls `b3mod::install_archive`: a `.b3mod` / `.zip` with mod.ini at the top or in its one top folder, unpacked into
  `mods/<id>/` through a hidden `.install-<id>` folder (discover skips dot folders) and a rename, all or nothing. Every
  entry is checked before anything is written: zip-slip (absolute paths, drive letters and any `:`, `.` / `..`, empty
  components), names that are not portable (control characters, `<>"|?*`, a trailing dot or space, Windows device
  names, not UTF-8), links (Unix S_IFLNK, a Windows reparse point), encryption, methods other than stored / deflate,
  ZIP64 and split archives, sizes (2 GB per entry, 16 GB in all, 100,000 entries; data longer than its header says is
  caught while inflating), the CRC, names that differ only in case, a name that is a file and a folder, a missing or
  broken mod.ini. An installed id is refused with `exists` set; the page then asks "Replace?" (the old folder is
  moved aside, the new one renamed in, the old one removed; mods.ini keeps its place and switch). The reader is our
  own (central directory from the end record; zlib's raw inflate); a zip read in place without unpacking is not done.
- **File conflicts shown.** `b3mod::conflicts(overlay)` lists every path more than one active mod has, its winner
  and the hidden ones; `conflicts_of` gives one mod's side. The page builds the next start's set as native_play does
  (`select` on mods.ini's enabled ids, then `Overlay::build(..., false)`: no file reads), shows "Conflicts: N files
  are in more than one mod that is on" at the top and, on each card that is on, "Wins over another mod for N files:
  KEY (mod), ..." and "Hidden by a mod above for N files: ...".
- **Requirements.** `[requires] other-mod = 1.2` (empty or `*`: any version). `select` drops a mod whose requirement is
  not in the selected set (not installed, off, unusable, older by `compare_versions`), repeating until nothing more
  drops (chains). The message says why (`x: it requires y 1.2 or newer, which is not on: left out`): native_play
  prints it (and with `--mods` exits 2, as for any missing id); the Mods page shows it on the card ("Off at the next
  start"). **Load-order rule:** requirements never reorder the list; the list alone decides which file wins. A mod
  that changes files of a mod it requires belongs above it; when it is below, `Selection::notes` says so (log and
  card).
- **Safe mode starts without mods.** The renderer decides safe mode for a window run (run_state.ini beside
  pc_settings.ini: two unclean runs in a row, `--safe-mode`, `B3_SAFE_MODE=1`; it then writes the file).
  native_play reads the same file first, without writing it, through the same `b3settings::start_run`, so both
  decide alike before the renderer is opened; when safe, it builds no overlay (whatever mods.ini or `--mods` say),
  logs `native_play: mods: SAFE MODE (...): this run starts without mods (N on in ...)` and passes
  `--mods-safe-off N`, which adds "YOUR N MODS ARE OFF FOR THIS RUN" to the window's safe-mode notice. Offscreen runs
  have no safe mode (and take no mods.ini).

### 1.3 As built (patches3): patches at run time, texture packs per mod

Code: `include/b3mod/mods.h` (`PatchEnv`, `Overlay::build(mods, hash_contents, env)`, `errors()`, `texture_packs()`,
the b3patch reader `read_patch_info` / `apply_patch`, which b3modtool now shares), `src/b3mod/mods.cpp`; the callers
native_play (the platform's disc), b3render (`--game`) and the launcher (the game folder); `include/b3asset/hdpack.h`
(`HdPack::open(folders)`, `hd_enable(dir, off, before)`). Tests: `tests/test_mods.cpp` (`patches_applied_and_cached`,
`patch_mismatch_disables_the_mod`, `patch_set_hash`, `texture_packs_in_load_order`, `manifest_patches_and_textures`),
`tests/test_hdpack.cpp` (`packs_merge_in_priority_order`); in the game `tools/modding/tutorial_check.sh` (below).
Every key and rule: [mod-format.md](mod-format.md) "Patches", "[textures]".

- **`[files] patches`** (default `patches`): `<folder>/<disc path>.b3patch`. `Overlay::build` reads each patch, reads
  the player's file through `PatchEnv::read_disc` (each caller's own disc reader, so a disc image works as a folder
  does), checks its size and FNV-1a 64 against the patch's source, applies it, checks the result against the patch's
  target, and writes it to `<mods folder>/.cache/<patch fnv>-<source fnv>.bin` (a temporary file and a rename; reused
  while its size and hash are the target's; the folder may be deleted at any time). The overlay file's host path is
  that cache file, so every reader (the game's GTFS and RW Audio reads, the renderer's discfs overlay, the launcher's
  art) takes it like a mod file; nothing else changed in them. Any patch that does not apply -- another version of the
  file, a damaged patch, no such file on the disc, a patch and a `data/` file for one path -- leaves **the whole mod**
  out, with `<id>: <patch>: <why>: the mod is off for this run` in `errors()` (and the mods that require it). native_play
  prints it and, as for any unusable mod, ends a `--mods` run with exit 2 (a window run from mods.ini goes on without
  the mod); the renderer prints it; the Mods page shows it on the card ("Off at the next start: ..."). A patch always
  applies to the disc's own file, never on top of another mod's.
- **The set hash with patches**: a patched path enters the mod hash as `patch <KEY> <patch size> <patch fnv>`, beside
  the `file` lines, by key. The patch names its source and target hashes, so the same patch mod on the same disc gives
  the same set hash on every machine (test `patch_set_hash`: two folders, two caches, names in another case); the cache
  file's place never enters it. Data-only mods hash as before. A code mod's `code` lines (sdk2b, §2) follow the `file`
  and `patch` lines.
- **`[textures] pack = <folder>`**: a PCSX2-format replacement folder (the HD pack's format, src/b3asset/hdpack.cpp).
  The renderer, after building the overlay, passes `texture_packs()` (the active mods' folders in load order) to
  `hd_enable(user pack, off, before)`, which indexes them first and the player's pack last as one `HdPack` (a name
  already indexed keeps its first file): a mod's replacement beats a later mod's and the player's for the same
  texture, and the player's pack fills the rest. `--no-hd` / the HD textures setting turn off only the player's pack;
  a mod's textures follow the mod. No mod with `[textures]`: `hd_enable` is called as before. The textures are not in
  the set hash (the game never reads them; a replay with other textures replays the same).
- **Proof** (`tools/modding/tutorial_check.sh`, in the game, offscreen): tutorial 01's mod made of the two
  `.b3patch` files alone turns both logos blue in the title and menu shots, the cache files equal the imported TXDs,
  and the race shots equal the run without mods; a patch made against another `FRONTEND.TXD` ends a `--mods` run
  with its message; a `[textures]` mod made from an exported picture (FRONTEND.TXD 203 at twice its size, red and green
  swapped, as a DDS named by its PCSX2 key) turns the title logo green.

Still to do in part 1:
- `[requires] conflicts = x, y` (mods that must not be on together), and a zip mod read in place.
- A b3modtool command that names a texture's PCSX2 replacement file (today: `b3hdkeys key`, a render tool), so a
  `[textures]` mod can be made from `txd export` without PCSX2.

## 2. Native code-mod SDK

### 2.1 Loading

```ini
[code]
windows = bin/win64/hello.dll
linux   = bin/linux-x64/hello.so
macos   = bin/macos-arm64/hello.dylib
gameplay = no           # the author's statement (§6); "yes" for anything that changes physics / AI / scoring
```

native_play loads the library for its platform (LoadLibraryExW with an absolute path / `dlopen(RTLD_NOW | RTLD_LOCAL)`)
after the native table and the port's own features are installed and before the game's first instruction, in load
order, then calls its one export:

```c
B3MOD_EXPORT int b3mod_main(const B3ModApi* api, B3ModSelf* self);   /* 0: loaded; else refused (its log says why) */
B3MOD_EXPORT unsigned b3mod_built_for(void);                           /* the B3MOD_API_VERSION of the header it saw */
```

A mod registers everything (hooks, settings rows, callbacks) inside `b3mod_main`; registration after it returns is
refused (the hook table is fixed for the run: determinism, and no locking on the hot path). The library's bytes are
hashed into the mod hash (`file <code path> ...` lines, as data files).

### 2.2 The ABI (`sdk/include/b3mod_api.h`, C, versioned)

One struct of function pointers, `size` and `version` first; additions go at the end (a mod checks `api->size`
before using a newer member), a change to an existing member bumps `B3MOD_API_VERSION`. The port supports its current
version and the one before. All strings UTF-8, all guest addresses 32-bit EE addresses.

| Group | Functions (sketch) |
|---|---|
| info | `version`, `port_version()`, `vblank()` (the virtual VBlank count: the clock to use), `mod_dir(self)` |
| log | `log(self, level, text)`: into native_play's log, prefixed with the mod's id |
| hooks | `replace(self, addr, fn, user)`, `wrap(self, addr, fn, user)`; inside a hook `next(ctx, regs, result)` runs what is below it (the generated function, or the next layer); `call(self, addr, regs, result)` calls any game function. `B3ModRegs` = a0-a3 / t0-t3 (64-bit, plus the high halves for 128-bit arguments), f12-f19 raw bits, sp; `B3ModResult` = v0 (128-bit), v1, f0 -- the EE ABI as `b3host::CallArgs` / `CallResult` (include/b3host/host.h), so no C++ type crosses the ABI |
| memory | `read(self, addr, dst, n)`, `write(self, addr, src, n)` (bounds-checked: EE RAM, scratchpad; false otherwise), `alloc(self, bytes, align)` from the **mod arena**: guest RAM the loader reserves and hands out in load order and call order, so addresses are the same every run. Built in Phase 2a: `b3native::GuestArena` (src/native/hook_table.h), 0xC0000 up to the ELF (0x100000); the port's own regions in its first 32 KB at the addresses they always had (pc_menu.h 0xC0000, date_format.h 0xC0800, custom_soundtrack.h 0xC0C00, now_playing_popup.h 0xC0F00, online_message.h 0xC1200, compile-time constants), the mods' allocations from 0xC8000 ([hooks.md](hooks.md) "The mod arena") |
| settings | `add_setting(self, const B3ModSetting*)`: a toggle, a choice or a slider with a key, label, values and default; `get_setting(self, key)`. Rows appear in PC SETTINGS under a MODS tab and on the launcher's Mods page; values in `<user data>/mod-settings/<id>.ini`. A change made while the game runs reaches the mod at a VBlank boundary and is written into the pad recording (`set <vblank> <id>.<key> <value>`), so a replay applies it at the same VBlank |
| overlays | `texture(self, w, h, rgba)` -> id; `quad(self, x, y, w, h, rgba, texture)`, `text(self, x, y, size, rgba, utf8)` in the game's 640 x 448 screen (the widescreen layout anchors it like the HUD) or window fractions; queued per game frame on the game thread and handed to b3render beside the frame (the feed's side channel, as the PC SETTINGS page's own), drawn in the 2D stage after the HUD with glyphdraw.h's font or the game's fonts. Drawing never touches guest state |
| events | `on_vblank(self, fn, user)` (game thread, after the platform's VBlank hooks), `on_frame` (each submitted frame), `on_shutdown` |
| files | `open_data(self, path)` / `read_data`: the disc through the overlay (read-only), and the mod's own folder; no other host file API is offered (a library can of course call the OS: §2.5) |

### 2.3 Hooks, and what they can reach

- **Addresses are the retail ones** (EE virtual addresses of SLUS-21050, as the decomp, `gen/native/manifest.tsv`
  and `config/symbol_addrs.txt` name them). The port supports exactly one executable, so an address means the same
  function in every port version; what changes between versions is how much of the game is native, not the addresses.
- **Built on the native table** (`b3host::NativeOverride`, `src/native/native_table.h`). As built in Phase 2a: one
  **hook table** (`b3native::HookTable`, `src/native/hook_table.h`; [hooks.md](hooks.md)) replaced the ad-hoc chaining
  (each port feature copied the table and called `set_native_overrides`: pc_menu.h, card_silence.h, date_format.h,
  custom_soundtrack.h, menu_mouse.h, the debugging wrappers debug_health.h, time_trace.h, diff_check.h,
  poison_check.h, and the switch-off of explore_standins.h). It copies the installed rows once and publishes them;
  fixed layers per address, bottom to top: the row (the generated function, or the platform / libm row at that
  address) -> a mod's `replace` (`kReplace`) -> the port's own wrappers (`kPort`, in install order) -> the mods'
  `wrap`s (`kWrap`, top: the first in the load order). Each hook gets the entry below it as `next` (rewritten when a
  layer goes in beneath it). So a port feature keeps working over a mod's replacement, and a mod's wrap sees the game as
  the player does. An address without hooks runs its row's entry directly (no extra call), and the port's wrappers
  chain exactly as before, so with no mods the table is the old one. Installs are by owner (`port:<feature>`, later
  `mod:<id>`), all or nothing, and `seal()` (native_play, before the game's first instruction) ends them. The C++ API
  Phase 2b wraps: `install(owner, hooks)`, `Hook{address, layer, fn, &next}`, `bind_hook(address, layer, fn, user)`
  (a fixed pool of 1024 trampolines for hooks with their own data), `chain(address)` / `hooked_addresses()` for
  reports. Tests: `tests/test_hook_table.cpp`.
- **What dispatch sees.** A hook runs for every call that goes through dispatch: calls from code that is not native,
  virtual calls (`vcall`), function pointers (`gfn`), `call_guest`, and every call to an `extern "C"` decomp function
  (the generator routes C-linkage callees through `guest_fn` [C docs/decomp-on-host.md "Calls"]). A **direct C++ link
  between two generated units bypasses it** (a member function defined in a shared header). The SDK reference lists,
  per address, whether all of its callers dispatch; `config/mod_hooks.txt` names C++ callees the generator must call
  through dispatch, so a hook point modders ask for becomes complete in the next port release (decomp2host option; no
  cost for the rest). As built in Phase 2a: decomp2host puts a two-line prologue (`include/b3host/hook_route.h`) at the
  top of each listed definition; with a hook on the address, a call that did not arrive through the row's
  `traced_entry` (a direct link, a direct recursive call) is sent through `call_guest`, else the body runs (no hook:
  one load and branch). Not for constructors / destructors or struct parameters by value (the generator stops). The
  list is empty, so gen/native is unchanged; adding an address means `make native-bundle` and committing gen/native.
- **Refused**: an address with no table entry (not native, or inlined in the decomp), the platform tier (the
  `B3NATIVE_FIRST` rows and the platform's kernel / SIF / drive / VBlank functions, platform.h), unless the mod says
  `[code] platform = yes` *and* the user turned on "Developer mods" (PC SETTINGS or the launcher's Mods page); writes
  into the ELF's code ranges (they never run natively: a sign of a PS2 patch, so the log says to use a hook).

### 2.4 Conflicts

- Per address one `replace`. Two mods replacing one function: the higher one in the load order wins and the other mod
  is **not loaded at all** (a mod's registrations are all-or-nothing, so no mod runs half-installed), with a message
  in the log and on the Mods page ("conflicts with mod-x: both replace 0x003FBC20 func_003FBC20").
- `wrap`s chain in load order, any number per address (deterministic: the order is the list's).
- Settings keys are per mod (`<id>.<key>`); the arena never overlaps; two mods writing the same guest address are not
  detectable in general (documented; the RAM-hash tools show it).
- A mod may declare `[requires]` / `conflicts` (§1.2); the loader checks them before any library is loaded.

### 2.5 Trust

A code mod is a native program: no sandbox can stop a library from calling the OS. The API's limits are for
correctness and determinism, not security. So: the launcher marks mods with `[code]` ("contains code: runs as a
program on your PC"), asks once per mod id and set of code files (one hash over every library in its folder) before
the first start with it, and the docs say
to install code mods only from people you trust, preferably with source. Data-only mods cannot run code (the overlay
serves bytes to the game's own loaders; the decomp's parsers are what read them, as on a PS2 with a modded disc).

### 2.6 Build template and examples

`sdk/` (in the repo and in each release zip): `include/b3mod_api.h`, `CMakeLists.txt` (one target, `-fvisibility=hidden`,
`B3MOD_EXPORT` from the header; `cmake -B build && cmake --build build` on all three systems) and a plain `Makefile`
(Linux / macOS / MinGW-w64), and `tools/modding/new_mod.sh <id>` writing a mod folder with mod.ini, a source file and
the build files. Per platform: Windows x64 (MinGW-w64 from the port's toolchain zip or MSVC: a C ABI, either works),
Linux x86-64 and arm64 (gcc / clang `-fPIC -shared`), macOS arm64 (clang `-dynamiclib`, ad-hoc signed; native_play is
built to allow loading libraries signed by others: hardened runtime with `disable-library-validation`, docs/mac.md).
Examples (`examples/mods/`, code only, no game data):

| Example | Shows |
|---|---|
| `hello-overlay` | a code-only mod: `on_frame` draws a small box with the race's VBlank counter |
| `menu-label-setting` | a settings row (Off / On) that swaps one GLOBALUS.BIN string through a wrap of the string lookup func_00271A00, as tools/native/date_format.h does |
| `tuning-preset` | a data + tuning mod (§4): a preset file, flagged `gameplay = yes` |
| `hello-texture` | a data mod: one texture replaced (needs §3's tools to make; Phase 1 ships its manifest and README) |
| `lua-speedo` | a script mod (§10.1): the player's speed and the race time in a box, a settings row, lap events (no build step) |

### 2.7 Determinism of code mods

Hooks run on the game thread, in call order, on virtual time; a mod that bases game effects on `vblank()` and guest
state (never the wall clock, never host randomness) replays exactly with the same set. The set hash covers the library
bytes and, from Phase 2b, the mod settings' values at start (settings changes during a run are in the recording).

### 2.8 As built (Phase 2b, sdk2b)

The SDK as built; the reference is [sdk-reference.md](sdk-reference.md). Where it differs from the sketch above, this
section counts.

- **The ABI** (`sdk/include/b3mod_api.h`, `B3MOD_API_VERSION` 1, mod API `api = 2`; `b3mod::kApiVersion` 2): info,
  log, hooks (`replace`, `wrap`, `next`, `call`, `has_function`), memory (`read`, `write`, `alloc`), settings
  (`add_setting`, `get_setting`), overlays (`texture`, `quad`, `text`), events (`on_vblank`, `on_frame`, `on_shutdown`),
  files (`data_size`, `read_data`). Its layout is frozen by `tests/test_code_mods.cpp`.
- **The loader** (`src/native/code_mods.h`): in native_play between the last port wrapper and `hooks.seal()`, in load
  order; bound hooks (`bind_hook`) with a C trampoline; all or nothing per mod, including its arena memory
  (`GuestArena::truncate`), its rows, callbacks and the writes it made in `b3mod_main` (deferred until it loaded). A mod
  that does not load is left out **with its files**: the game's overlay is rebuilt without it (with the same
  `PatchEnv`, so the other mods' patches come from the cache), the renderer is told (`b3r_mods_set`, which rebuilds
  its overlay the same way), and the set printed again. Its `[files]` patches go with it; its `[textures]` pack too,
  unless the renderer had already opened its HD textures when the call came (the pack then stays until the next run,
  with a line in the log: it changes only the picture, never the game or the set hash). The reasons go to the log and to `<user data>/mods-state.ini`, which the
  launcher's Mods page shows ("At the last start: not loaded: x conflicts with y: both replace ...").
- **Libraries**: `[code] windows / linux / macos` paths inside the mod; the templates put them in
  `bin/<windows|linux|macos>/<name>.<dll|so|dylib>`. Every library named is in the mod hash (`code <system> <path> <size>
  <fnv>` lines), so a folder hashes the same on every system; the set hash's text keeps its first line `b3mods api 1`.
- **Trust**: the launcher asks once per mod id and set SHA-256 (every library file in the mod's folder: `.dll`, `.so`,
  `.so.<n>`, `.dylib`, and the named library; turning the mod on, or starting the game), kept in
  `<user data>/mods-trusted.ini`; a window run refuses a mod whose id and set are not listed, and the loader hashes the
  set again right before it loads the library (Windows: held against writes until loaded); offscreen runs are not asked
  (test-only: the launcher never starts offscreen).
  **Developer mods** is one switch shared with the tuning console (§4.1): `developer_mods` in `pc_settings.ini`
  (PC SETTINGS > MODS > DEVELOPER MODS, or the launcher Mods page's button, which sets the same value), read by a
  window run; `--developer-mods` / `B3NATIVE_DEVELOPER_MODS=1` turn it on for one run (offscreen runs: off unless
  given). (sdk2b's test builds kept it as `[options] developer_mods` in `mods.ini`; that section is now ignored.)
- **Settings**: the values at the start come from `<user data>/mod-settings/<id>.ini` (window runs) or the defaults
  (offscreen); `--mod-set` / `B3NATIVE_MOD_SET` set them from the start or at a VBlank. A change (the page's MODS tab,
  the script) takes effect at the next VBlank boundary (`b3mod::LiveSettings::boundary`), before the mods' `on_vblank`.
  Recorded: pad recordings `set <vblank> <mod>.<key> <value>` (every row at VBlank 0, then each change), `.b3replay`
  settings `mod.<mod>.<key>`; a replay applies them and ignores the page. The values at the start are recorded, not
  hashed (the set hash names the code and the files; the recording names the values).
- **The MODS tab** of PC SETTINGS appears only when a loaded mod added a row (so a run without code mods draws the page
  as before). The launcher's Mods page shows each code mod's rows under its card (from the last start's report) and
  edits the values file.
- **Overlays**: the items a mod queues between two frames go to the renderer at the next poll as the host record
  'HMO1' (`include/b3mod/overlay_record.h`, at 0x41114000, only when they changed); the renderer draws them after the
  HUD in the 640 x 448 box (`src/b3replay/modoverlay.h`), in the window and in offscreen shots, under the PC SETTINGS
  page. Textures go to the renderer directly (`b3r_mod_texture`). Text uses glyphdraw.h's stroke font.
- **Templates**: `sdk/Makefile` (GNU make with a POSIX shell: Linux, macOS, MSYS2, and Windows from Linux with
  MinGW-w64), `sdk/build_windows.cmd` (MinGW-w64 and cmd.exe alone), `sdk/CMakeLists.txt`. `tools/modding/new_mod.sh`
  is not written: copying an example is the start.
- **Checks**: `tests/test_code_mods.cpp` (make test) and `tools/modding/sdk_check.sh` (both examples built from sdk/ and
  run in the game).

## 3. Asset tools: `b3modtool`

**As built (3a, modtool1):** `tools/modding/b3modtool.cpp`, `src/b3modtool/`, `include/b3modtool/modtool.h`, tests
`tests/test_modtool.cpp`; user guide [b3modtool.md](b3modtool.md), delta format [b3patch.md](b3patch.md). `list`,
`extract`, `txd list / export / import`, `strings export / import`, `diff` / `apply`, `pack`, `verify`, `roundtrip`.
Census on the US disc: 415 of 415 TXD rasters (2 files) and 4017 of 4017 strings (2 files) identical after export ->
import; a TXD import writes the rasters back through the GS memory model (the upload's bytes read back where the
transfers put them), so the file keeps its size and layout. Not yet: textures inside STATIC.DAT / ENVIRO.DAT / car
files (the same write-back applies: `roundtrip <disc> rasters` writes 9535 of 9535 of them back identically; an
import needs the structural raster walk instead of the header scan), mip levels are rebuilt by a 2x2 average.
`b3modtool` ships in the Windows zip and the Linux tarball, beside the launcher. Tutorials 01 (a texture) and 02
(menu text) follow these tools end to end (§5; `tools/modding/tutorial_check.sh`).

**As built (3b, modtool3b):** `vdb export / import` (`src/b3modtool/vdb.cpp`: CGtHash, the layout reader / writer,
the `<group> | <name> = <value>` text; names from `src/b3modtool/vdb_names.inc`, generated by
`tools/modding/tuning_registry.py --names-table --cxx`, names only) and `audio list / export / import`
(`src/b3modtool/audio.cpp`: PS-ADPCM decode as the SPU2 mixer and an encoder, WAV, RWS streams and AWD / HWD / LWD
banks; unedited frames are kept, so an unedited WAV gives the original bytes; an item may change length). Census on the
US disc: VDB.XML identical from its text (7,525 of 7,525 values), 108 of 108 streams (1,016 items) and 174 of 174 banks
(1,575 waves) identical through WAV, each file's first item made longer read back with the rest unchanged.
`[files] patches` at run time: built (patches3, §1.3).

**As built (3c, first half: export, models3c):** `car list / export`, `track list / export`, `preview`
(`src/b3modtool/carexport.cpp`, `trackexport.cpp`, `carlist.cpp`, `gltf.cpp` (JSON, the glTF writer and its check),
`gltfread.cpp`; `include/b3modtool/export3d.h`, `gltf.h`). glTF 2.0 (.gltf + .bin + PNG) or OBJ / MTL; cars by the
game's names (the launcher showroom's), every LOD and part as named nodes at their frames, the paints as
`KHR_materials_variants`, the lights as empties; routes by section (streamed unit: high and LOD meshes, instances,
props), backdrops and water, the STATIC.DAT materials and textures, the ENVIRO.DAT sky textures. The game's space is
glTF's (right-handed, Y up, metres): no conversion. For the import, each export keeps the stored vertex fields
(`_B3_*` attributes) and a sidecar (`.b3car.json` / `.b3track.json`: source hash, every header field and offset, the
material words, VU batch sizes, an order-free triangle hash per mesh). Census on the US disc
(`build/model_export_check`, `tools/modding/gltf_check.py`): 67 of 67 player cars and 37 of 37 routes (2,838 sections)
valid, read back with the renderer's own decode's counts (cars: 268 of 268 LODs, vertices, triangles and bounds
exact). Not exported: the collision hull and trees, line meshes, the generated sky dome. Import: cars and routes done
(below).

**As built (3c, second half: car import, import1):** `car import <disc> <model.gltf | .glb | export dir> <mod dir |
out.BGV>` (`src/b3modtool/carimport.cpp`; the paints `encode_raster_paints` in `src/b3modtool/txd.cpp`). The mesh packet
writer is the decode's inverse in the disc files' own form (file-formats/car.md "How the packets are written"): an
unedited export comes back as the disc's file byte for byte. Meshes are matched to entries by their `extras` or names
(a modelling tool's `.001` ignored); the same triangles in any order (a tool's re-save) keep the file's stream (the
model's normals where they turned); an object transformed as a whole keeps the file's stream with the transform applied;
the same index list keeps the strips and batches with the new vertex fields (moves, UVs, normals); other new triangles
get new strips (greedy, at most 52 vertices per batch, the winding kept, strips carried
across batches); a packet that grows moves into freed packet space or to the end of the file (entry offset and +0x08
follow), refused over memory block 1's 466,944 bytes. Object transforms are applied to the vertices (the part frames
stay the file's); normals and influences come from `_B3_NRM_*` / `_B3_INFL_*` while they still fit, else from `NORMAL` and
the nearest original vertex. Paints: a recolour changes only that paint's CLUT; a picture edit re-quantises all paints
together (one index picture, a colour per paint for each entry). Refused with a message, nothing written: unknown
meshes (no new objects), positions not finite or beyond 1 km, UVs outside 0..16, influence zones the file does not use,
other-size pictures, an export from another file. Every written file is decoded again and compared with what was
meant. Output: a b3patch in `<mod>/patches/` (and a `mod.ini` when there is none) or the whole file. Not changed: the
LOD / list / entry structure, the frames, lights, hull, sound structures, unknown areas.

**As built (3c, track import, trackimport1):** `track import <disc> <model.gltf | .glb | export dir> <mod dir |
out.b3mod> [--sidecar] [--restrip]` and `track check` (`src/b3modtool/trackimport.cpp`; what both imports share --
the strip builder, the packet buffer, node matrices, reading a .gltf / .glb -- moved to `src/b3modtool/meshimport.cpp`,
`include/b3modtool/meshimport.h`, the car import unchanged: 67 / 67 cars as before). Visual meshes only: the sections'
high and LOD meshes (STREAMED.DAT), backdrops, signs, water, instance and prop models (STATIC.DAT); the STATIC.DAT
textures through their PNGs (`encode_raster` in place); which texture a material draws; which material a backdrop or
instance model is drawn with (same type only). The writer is the decode's inverse in the disc's one form
(file-formats/track.md "How the packets are written": seven layouts, three of them transposed by four, ITOP on a
quadword): every one of the 140,896 meshes the files name comes back byte for byte. Primitives are matched to their
chains by their `extras` (file, chain offsets), else by section and material (a unit has one mesh per material), a
backdrop group and material, or an instance model's name. As for cars: the same triangles in any order keep the file's
stream; a world-space mesh moved as a whole (its node) keeps it with the transform applied; the same index list keeps
the strips with the new fields; other triangles get new strips (80 / 68 type 1 / 76 instance models per batch). Colours:
`COLOR_0` against `_B3_COLOR_*` per channel (a painted channel stored as its shown value / 4). A packet that grows moves
into freed space, its block's free end, or STATIC.DAT's end; a STREAMED.DAT block grows by 32 KB up to its slot
(0x28000 high, 0x18000 LOD [L memMgrInit]), the later blocks and the unit table following; STATIC.DAT up to 0x580000
(block 5 in the two-stream layout [C GetBlockSize 0x3E7850]) or its own size. The meshes' culling boxes (and backdrop
objects', instance types') grow to hold new positions. Refused: unknown meshes (no new objects), a section's material
(the render tables), another type's material, positions beyond 100 km / 1 km, UVs outside -8..8, a placement moved,
added or removed, another-size pictures, animated / untextured texture swaps, a block over its slot, an export of other
files. Every written file is read again (each mesh names the bytes meant, the rewritten ones decode to their streams,
the renderer's reader takes the files). Kept: the collision trees (visual only: a raised road is drawn higher, the
cars drive on the old one), `GAMEDATA.BGD` (AI, traffic, race paths: not read), the placements, the render tables, the
materials' GS state, the line meshes, the instances' LOD models, the streaming quads, the irradiance, ENVIRO.DAT. The
export writes the stored fields as one float SCALAR attribute per component (`_B3_COLOR_R` .. `_A`, `_B3_NRM_X` .. `_W`,
`_B3_LIGHT_X` .. `_W` for type 5's lighting vectors, `_B3_W`): Blender keeps floats, and its importer pairs a mesh's
`_` attributes with their data in a set's order, right only when all have one form (§11.1).

The plan: one CLI (`tools/modding/b3modtool.cpp`, built by `make modtool` and in each release), on the decoders the port already
has (src/b3asset: discfs, raster / txd, car, track, png, export; b3assetdump's commands become its export half). It
reads the user's disc (a folder or the image, as `--game`) and writes only where told.

| Command | Does | Round trip |
|---|---|---|
| `list <disc> [glob]` | every file: path, size, kind (by extension and header) | -- |
| `extract <disc> <glob> <dir>` | copies files out of the folder or image (for editing, never for sharing) | bytes |
| `txd list / export / import` | RenderWare PS2 texture dictionaries (FRONTEND.TXD, GLOBAL.TXD, the textures inside STATIC.DAT / car files): each raster to PNG (with its CLUT), and back: a PNG of the same size quantised to the raster's own format (PSMT8 / PSMT4 + CLUT, or 32-bit), mipmaps rebuilt | export -> import unedited gives the same bytes |
| `strings export / import` | GLOBALUS.BIN and the other string tables (UTF-16): id, text as UTF-8 lines; import keeps the ids and rebuilds the offsets (lengths may change) | yes |
| `car export` / `car import` | a car (BGV / BTV): OBJ / MTL / PNG and glTF, per LOD; the edited glTF / GLB and paint PNGs back (import1) | an unedited export gives the same bytes (67 / 67 cars) |
| `track export` / `track import` | a route (STATIC / STREAMED / ENVIRO.DAT): meshes, materials, textures (docs/track-format.md); the edited glTF / GLB and texture PNGs back as patches of STATIC.DAT / STREAMED.DAT, the visual meshes (trackimport1) | an unedited export gives the same bytes (37 / 37 routes) |
| `audio list / export / import` | RWS streams (music, crash, movie audio: TRACKS/*.RWS, OVID/MOVIE.RWS) and AWD banks (SOUND/): segments / waves as WAV; import encodes PS-ADPCM at the segment's rate | import: same structure, new audio |
| `vdb export / import` | DATA/VDB.XML, the tuning defaults (§4) as `name = value` text | yes |
| `diff <orig> <modded> <patch>` / `apply` | a binary delta of one file against the user's own (`b3patch`: the source's size + hash, copy / insert runs): a mod ships deltas, never the game's file | apply(diff) == modded |
| `pack <mod dir>` / `verify <mod>` | a `.b3mod` zip; verify checks the manifest, paths, sizes, and that no file equals a disc file (§8) | -- |
| `roundtrip <disc>` | every import / export pair over the whole disc: the census that backs the "round trip" column | the check |

A format is "importable" only when `roundtrip` gives identical bytes for every file of that kind on the disc;
until then it is export only, and the file-format doc says so. Formats with no decoder yet (KFS, STAGEHED / LOADSCRN /
PRGDATA.BIN, GAMEDATA.BGD, VLIST / TLIST.BIN, the M2V movies' replacement) are listed as such in the docs; the decomp's
loaders are the reference for each.

Delta mods at run time (built, patches3: §1.3): `[files] patches = patches` (the default):
`patches/DATA/GLOBALUS.BIN.b3patch` is applied to the disc's file when the overlay is built (checked against the
patch's source size and hash; a mismatch disables the mod with a message), the result kept in a cache file under
`<mods folder>/.cache/` and served like any overlay file. The set hash covers the patch, so it is the same on every
machine with the same disc.

`verify` / `pack` (patches3): an export of b3modtool's inside a mod (a folder with the `txd.txt` / `audio.txt` that
`txd export` / `audio export` write, a `strings` or `vdb` text with more than half of its table) is an error unless
`--allow-exports`; `pack --game` refuses a file in `data/` that is half or more the disc's (the warning `verify`
gives) unless `--force`, and says how to make the patch instead.

## 4. Tuning console

- **The registry.** Retail registers its tuning variables with a variable database: `GtComms::CGtCommsDatabase::AddEntry`
  for floats (0x0021BB60: pointer, three strings, int, float min / max, int), ints (0x0021BD80) and vectors (0x0021B9B0)
  [C config/symbol_addrs.txt "d4/vdb"]: 705 call sites in 26 functions, listed in
  [file-formats/tuning-registry.md](file-formats/tuning-registry.md) (generated by `tools/modding/tuning_registry.py`).
  The game's defaults come from `DATA/VDB.XML`, which despite its name is binary: a 0x14-byte header, (value, key)
  records sorted by signed key, 16-byte-aligned blocks for vectors and arrays, and per-config-file enable records. The
  key is `CGtHash::CalculateHash` (0x0021BFA0: CRC-32's table, arithmetic shift, no inversion) of name + group + "/" +
  file. Defaults are applied at each AddEntry (`SetDefaultElementValue` 0x0024FFC0) and once at load
  (`InitialiseDefaultValues` 0x0024FE90) [C; 6,603 of the disc's 7,525 keys matched to registered names:
  [file-formats/vdb.md](file-formats/vdb.md)].
- **Exposed in the game.** A port wrapper on the three AddEntry rows records each entry (the variable's guest address,
  its type, its names / group, the range) in a host-side registry, in registration order (the same every run). A
  console page lists groups and variables with their current values; edits write the guest variable at a VBlank
  boundary and go into the pad recording (`tune <vblank> <name> <value>`), so replays reproduce them. Where: PC
  SETTINGS > DEVELOPER (shown only with "Developer mods" on), and an overlay opened with a key (F9) for live tuning in a
  race. Off (the default), nothing is wrapped and nothing recorded.
- **Presets per mod.** `[tuning] preset = tuning/handling.ini` (`name = value` lines; names as the registry shows them,
  or `0x<hash>`). Applied right after InitialiseDefaultValues, in load order (a later mod's value for a name loses to an
  earlier mod's). A preset is gameplay-changing by nature: the mod is flagged as such (§6) whatever it says. The console
  can save the current edits as a preset into a new mod folder. `b3modtool vdb` reads / writes VDB.XML for a data-only
  route (the overlay then serves a whole new VDB.XML, or its b3patch).

### 4.1 As built (tune4)

Code: `include/b3tune/tune.h` (the model, header only: the key, the registry, values, ranges and steps, presets,
events, scripts), `include/b3tune/tune_abi.h` (native_play -> renderer, `b3r_set_tune_hooks`),
`tools/native/tuning_console.h` (the game's side), `src/b3replay/tuneconsole.{h,cpp}` (the DEVELOPER tab, the F9
overlay), the `[tuning]` section in `include/b3mod/mods.h`; tests `tests/test_tune.cpp`. User guide:
[tuning.md](tuning.md); tutorial [04](tutorials/04-a-tuning-preset.md).

- **The registry.** A port-layer hook set ("port:tuning") on the three AddEntry rows and on InitialiseDefaultValues
  0x0024FE90. All 26 registering functions call AddEntry through dispatch (`guest_fn`: checked in gen/native), so the
  wrapper sees every registration. After the row returns, the wrapper reads the entry the call filled (the slot at
  database +4; +0 variable, +4 / +8 max / min, +0xC / +0x10 / +0x14 name / group / file, +0x18 key, +0x1C count,
  +0x1D flags, +0x1E type: 0 when the file is disabled) and records it host side (`b3tune::Registry`: one entry per key
  and address, in first-registration order, re-registrations counted). A variable is live while its slot still holds
  it (type, address and key). Installed only when the console is on (DEVELOPER MODS, `--tuning on`), a mod has a
  preset, `--tune` edits, or a recording being played has edits; otherwise nothing is wrapped.
- **The count.** The 705 of the registry table are call sites. A run counts calls: once per static variable at the
  boot, once per car / camera / sound object / route per race preparation (the result of the offscreen race scenario
  is in tuning.md "How it works"); code a run never reaches registers nothing.
- **Edits.** From the DEVELOPER tab, the F9 overlay (renderer thread: queued) or `--tune` (resolved by name at its
  VBlank): taken in native_play's VBlank hook, clamped to the range, written to every live variable with the key, kept
  as session overrides (re-applied at each later registration of the key, after the preset), and recorded as
  `tune <vblank> 0x<key>[<element>] 0x<word>` (pad recordings) / SETS `tune` (replay files). Playback queues the
  recorded events and applies them in the same hook on the same VBlank; the live sources are off then.
- **Presets.** `[tuning] preset` (a path inside the mod, checked like `[files] root`); the file's bytes join the mod
  hash (`tuning <path> <size> <fnv>`). Applied after each AddEntry (after the game's SetDefaultElementValue) and to every
  registered variable after InitialiseDefaultValues; per element the first mod in load order wins, within a preset key >
  `[file]` > plain name, later line on a tie; clamped to the range. A preset mod is gameplay-changing: log, Mods page,
  `gameplay` line / note in recordings.
- **Not yet**: the DEVELOPER tab was seen only in an offscreen shot (the page's tab bar with DEVELOPER as its 7th tab,
  DEVELOPER MODS on, `--tuning on`); its rows and the F9 overlay have not been seen in a window run; a search in the
  console; `[tuning]` does not bump the mod api (an older port ignores the section with a note, so a
  preset mod there runs without its values).

## 5. Documentation layout (`docs/modding/`)

| File | Content |
|---|---|
| [`README.md`](README.md) | the index: what can be modded today, where to start, every page (done, tutorials1) |
| [`getting-started.md`](getting-started.md) | installing and ordering mods, making a data mod, the manifest (done, Phase 1) |
| [`b3modtool.md`](b3modtool.md) | every b3modtool command, the round-trip census (done, Phase 3a) |
| [`b3patch.md`](b3patch.md) | the delta format (done, Phase 3a) |
| [`mod-format.md`](mod-format.md) | the manifest and every section (`[files]`, `[code]`, `[script]`, `[tuning]`, `[textures]`, `[requires]`), mods.ini, archives, the set hash (done for api 1 in Phase 1b: `[mod]`, `[files]`, `[requires]`; patches3: `[files] patches`, `[textures]`; sdk2b: `[code]`; the other sections join with their phases) |
| [`file-formats/`](file-formats/index.md) | one page per format, each with its byte layout, decoder, round-trip state and decomp sources (done, moddocs1): [index.md](file-formats/index.md) (every file on the disc, its page and mod status), txd.md, strings.md, car.md, track.md (from docs/track-format.md), audio.md (RWS, AWD), movies.md, vdb.md, tuning-registry.md (every registered tuning variable), and the not-yet-decoded formats: kfs.md, bin-data.md (STAGEHED / LOADSCRN / PRGDATA.BIN), gamedata-bgd.md, lists.md (VLIST / TLIST.BIN), system.md |
| [`sdk-reference.md`](sdk-reference.md) | b3mod_api.h, written from the header's comments; the ABI rules, versioning, threading, loading, trust (done, sdk2b) |
| [`lua-reference.md`](lua-reference.md) | script mods: `[script]`, the sandbox, limits, determinism, the `b3` table, views, events, hot reload (done, lua6) |
| `hooks.md` | finding a function (the decomp, the names layer docs/names-layer.md, gen/native/manifest.tsv), the address list with dispatch coverage, writing a wrap, calling the game |
| [`tutorials/01-replace-a-texture.md`](tutorials/01-replace-a-texture.md) | b3modtool txd list / export / import, a mod folder, verify, see it in the game (the title and menu logos turned blue: FRONTEND.TXD 203, GLOBAL.TXD 4), then the patch route: diff, pack, and `apply` on the player's side until 3b (done, tutorials1) |
| [`tutorials/02-change-menu-text.md`](tutorials/02-change-menu-text.md) | b3modtool strings export / import with a partial text, the change on the main menu (SINGLE EVENT -> QUICK RACE and its help line), translations (done, tutorials1) |
| [`tutorials/03-a-settings-row.md`](tutorials/03-a-settings-row.md) | a code mod: a PC SETTINGS row that changes something the player sees (examples/mods/menu-label-setting) (done, sdk2b; checked by tools/modding/sdk_check.sh) |
| [`tuning.md`](tuning.md) | the tuning console, presets, edits and their recording, determinism (done, tune4) |
| [`tutorials/04-a-tuning-preset.md`](tutorials/04-a-tuning-preset.md) | the console, saving a preset, sharing it as a mod (gameplay-changing: what that means for replays and ghosts) (done, tune4) |
| [`tutorials/05-an-overlay.md`](tutorials/05-an-overlay.md) | a code mod drawing on top of the game (examples/mods/hello-overlay) (done, sdk2b; checked by tools/modding/sdk_check.sh) |
| [`tutorials/06-a-lua-script.md`](tutorials/06-a-lua-script.md) | a script mod: the speedometer box (examples/mods/lua-speedo), a settings row, lap events, a race_check run and its replay, hot reload (done, lua6) |
| [`tutorials/09-car-mods-in-blender.md`](tutorials/09-car-mods-in-blender.md) | the Blender add-on B3 Car Tools: install, import a car from the disc, scale the body, Check, a recoloured paint, export to a patch mod or .b3mod, the refusals (written, blender1; not yet followed in a real Blender) |
| `determinism.md` | what mods mean for replays, ghosts, bug reports; how to keep a code mod deterministic |
| `legal.md` | §8, for mod authors |
| `publishing.md` | packaging (`b3modtool pack`), versioning, `api`, requirements |

## 6. Determinism and safety

- **No mods = byte-identical.** With no active mod native_play builds no overlay (`disc::set_overlay` keeps null) and
  installs no hook; the log gains the line `native_play: mods: none`, nothing else. Offscreen runs read no mods.ini.
  Checked by the golden recording (§9 Phase 1).
- **The set hash everywhere.** Run log (start of every run), bug report (the logs and mods.ini), pad recordings
  (`mods` line; replays warn on a different set), ghosts (Phase 2b: `.b3ghost` gets the set hash; a ghost made with
  another gameplay-changing set is shown tagged and never counts as a best lap of the unmodded game), screenshots'
  metadata later.
- **Gameplay-changing.** A mod is gameplay-changing when it has a tuning preset, a code hook marked `gameplay = yes`,
  or replaces a file the simulation reads (track collision / AI data `TRACKS/*/GAMEDATA.BGD`, car handling in the car
  files, VDB.XML) [L: the list is refined as formats are decoded]. The Mods page shows "changes gameplay"; times and
  ghosts made with such a set are kept apart (media1, `src/b3replay/records.h`: a set with a tuning preset, a code mod
  marked `gameplay = yes` or a script mod not marked `gameplay = no` keeps its ghosts and speedrun best times in
  `ghosts/modded-<hash>/` and `speedrun/modded-<hash>/`, the hash over the gameplay-changing mods alone
  (`b3mod::gameplay_set_hash`: a texture mod added or moved keeps the folder); the plain game's folders list only their own files;
  leaderboards later). Purely visual or audio mods are not.
- **The golden checks run without mods** (`baseline.sh` / `race_check.sh` pass no `--mods`; an offscreen run ignores
  mods.ini). A mod's own checks run with `MODS=`.
- **Failure behaviour.** A broken manifest, a newer api, a missing required mod: that mod is not loaded, the rest are,
  the reason is in the log and on the Mods page. A mod that crashes the game: the crash report names the active set
  (the log's first lines). Safe mode (after two unclean starts, or asked for: b3settings/safemode.h) starts with
  mods off and says so (Phase 1b, §1.2).

## 7. Versioning and compatibility

- `api` in the manifest = the mod API version (`b3mod::kApiVersion`, `B3MOD_API_VERSION`). 1 = data overlay (this
  phase); code mods arrive as api 2 with the ABI v1 struct (the struct carries its own size, so additions within api 2
  need no bump); script mods (§10.1) as api 3, on the same ABI v1 struct (it did not change). A port loads mods of its api and older ones it still supports; a newer api is refused with "a newer port
  is needed".
- The game side never changes: one executable, fixed guest addresses, the same file formats. What can change between
  port versions: which functions are native and dispatched (the SDK reference per version), the hook layers, the
  renderer. The release notes list hook-point changes.
- Mods version themselves (`version`); `[requires]` names a minimum version of another mod.

## 8. Legal hygiene

- The tools work on **the user's own disc** (a folder or image they made); nothing of the game ships with the port,
  the kit or the examples (this repo: no game data in git, the examples are manifests, READMEs and code).
- **Mods ship their own content and diffs**: new textures, new text, new sounds, and `b3patch` deltas for files they
  change partly. Never a copy of a game file, even modified (a whole modified GLOBALUS.BIN is the game's text with one
  line changed: ship the patch, or the strings you changed). `b3modtool verify` warns when a mod file equals a disc file
  or is mostly one (the deltas' share of copied bytes).
- Mods made from other people's work need their permission; the docs say so and suggest a LICENSE file in each mod.
- getting-started.md, legal.md and the launcher's "Open mods folder" note repeat this in plain words.

## 9. Phases and acceptance checks

| Phase | Content | Acceptance |
|---|---|---|
| **1 (done, modkit1)** | §1.1: manifest, mods.ini, overlay in the platform's disc layer, `--mods` / `--mods-dir`, the set hash in the log / pad recordings, mods.ini in bug reports, launcher Mods page, `tests/test_mods.cpp`, `tools/modding/text_mod_check.sh`, getting-started.md, `examples/mods/hello-texture` | `make test` (test_mods, test_report); golden compare PASS with no mods (`JOBS=1 RATE_RUNS=0 tools/golden/baseline.sh` + `compare.sh`); text_mod_check PASS (the mod's text on the main menu, race shots equal); Windows cross build |
| **1b (done, modkit1b; `[textures]` done, patches3)** | §1.2: renderer readers overlay-aware (the disc layer's overlay, the set passed to b3render), Add mod from a zip / .b3mod, file conflicts on the Mods page, `[requires]`, safe mode without mods, mod-format.md; §1.3: `[textures]` packs per mod above the player's HD pack | `tools/modding/track_mod_check.sh` PASS (a modded STATIC.DAT palette drawn by the renderer, race shots changed towards magenta only); test_mods (crafted zips: zip-slip, links, sizes; conflicts; requirements; the disc overlay; texture packs in load order); test_hdpack (the merge order); `tutorial_check.sh`: a `[textures]` mod changes the title logo in the shots; golden compare PASS without mods; Windows cross build |
| **2a (done, hooks2a)** | the hook table refactor (one table, fixed layers; the port's wrappers moved onto it; the guest-RAM arena; config/mod_hooks.txt's route prologue, the list empty) | golden compare PASS (no behaviour change); test_hook_table (layer order, all-or-nothing install, conflicts) |
| **2b (done, sdk2b; macOS build of the examples and CMake not run yet)** | the SDK (§2.8): b3mod_api.h v1, the loader in native_play, trust in the launcher, settings rows (PC SETTINGS MODS tab, the Mods page, recording `set` events), overlays through the feed, sdk/ templates (Makefile, build_windows.cmd, CMakeLists.txt), examples hello-overlay and menu-label-setting, sdk-reference.md, tutorials 03 and 05; a code mod may also carry `[files]` (plain files and patches, patches3), a `[textures]` pack and a `[tuning]` preset (tune4), which load with it and are left out with it when its code does not load (a script mod the same, lua6) | each example builds on Linux, Windows (MinGW) and macOS from sdk/ alone; a race_check with each example: its effect seen (log / shot), and its replay equal; golden without mods PASS; a replace / replace conflict reported and the loser not loaded. **PASS** (2026-11-12): both examples built from sdk/ on Linux (make) and for Windows (build_windows.cmd with the verified MinGW-w64 toolchain); race_check STRICT PASS with each: hello-overlay's box and counter in the menu and race shots, menu-label-setting's QUICK RACE on the main menu after `--mod-set 16000:...=On`; `tools/native/replay_check.sh` PASS for both (the .b3replay played back with every log equal; the setting change recorded at VBlank 16000 and replayed); golden compare PASS without mods; the conflict in `tests/test_code_mods.cpp` and in a game run (`replace-b conflicts with replace-a: both replace 0x00271A00 func_00271A00`, the set printed again without it); `make test` (test_code_mods) |
| **3a (done, modtool1)** | b3modtool list / extract / txd / strings / diff / apply / verify / pack / roundtrip | `b3modtool roundtrip` identical for every TXD and string table on the disc (PASS: 415 / 415 rasters, 4017 / 4017 strings); `make test` (test_modtool); Windows cross build. Still open: tutorials 01 and 02 followed end to end in the game |
| 3b | audio (RWS / AWD), vdb **(done, modtool3b)**; `[files] patches` at run time **(done, patches3)** | roundtrip for each (**PASS**: VDB.XML 7,525 / 7,525 values, the file identical; audio 108 / 108 streams, 174 / 174 banks identical through WAV); `make test` (test_modtool: VDB layout and text, CGtHash, PS-ADPCM, synthetic RWS / AWD; verify's export and patch checks, pack's refusal; test_mods: patches applied and cached, a mismatch leaving the mod out, a patch mod's set hash the same from two folders and caches); in the game: `tools/modding/vdb_mod_check.sh` PASS (a b3modtool-made VDB.XML moves the follow camera in a race_check shot); `tutorial_check.sh` PASS with tutorial 01 as a patch-only mod (both logos blue) and a mismatching patch refused with its message. Open: the same set hash checked on a second machine (the test covers two folders on one) |
| 3c | car / track export (glTF) **(export done, models3c)**, car import **(done, import1)**, track import **(done, trackimport1)** | export: every player car and route valid glTF with the renderer's counts (**PASS**: 67 / 67 cars, 268 / 268 LODs exact; 37 / 37 routes, 2,838 sections), `make test` (test_modtool: JSON, the glTF writer and check, axes and units, the triangle hash, a synthetic car); car import (**PASS**, import1, 2026-11-13): `build/model_import_check` -- export -> import unedited byte-identical for 67 / 67 cars (and 67 / 67 with every mesh's vertices and triangles shuffled); every mesh restripped decode-identical 67 / 67 (7,022 / 7,022 entries, 268 / 268 LODs; +4.7 % vertices, 57 / 67 within the size limit); a quad added 67 / 67 decode-identical; LOD 3's body 1.3x and paint 0 recoloured exact 67 / 67; `make test` (test_modtool: the strip builder's windings and batches, every packet form, the synthetic car's identity through glTF and GLB, an object transform, the refusals, the paint recolour and re-quantising); `tools/modding/car_mod_check.sh` in the game: **PASS** (race_check PASS with and without the mod; the game read the race's five cars, `PVEH/COMP/CAR1, 4, 7, 8, 10.BGV`, from the mod's patches, the renderer built the same set; race shots 10700 and 11500 changed in 51 % and 17 % of their pixels: the player's car and the rivals 1.3x and blue); golden compare without mods: 140 PASS (rate: no rates, RATE_RUNS=0); track import (**PASS**, trackimport1, 2026-11-13): `build/track_import_check` -- every mesh's packets written again byte for byte (140,896 / 140,896), export -> import unedited byte-identical for 37 / 37 routes (STATIC.DAT and STREAMED.DAT; and 37 / 37 with every mesh's vertices and triangles shuffled); every mesh restripped decode-identical 37 / 37 (140,896 / 140,896 meshes, the renderer's decode high and LOD the same; +2.5 % vertices, 13 / 37 routes within the slots); the middle section raised 2 m and recoloured exact 36 / 36; a quad added decode-identical 36 / 36; the largest texture recoloured exact 37 / 37, a material's texture swapped 37 / 37; `make test` (test_modtool: the writer in every layout, a synthetic route's identity through glTF and GLB, an object move, a painted colour, a block grown by new triangles, the refusals); `tools/modding/track_edit_check.sh` in the game: **PASS** (race_check PASS with and without the mod; the game and the renderer read US/C2_V2's STREAMED.DAT from the mod's patch; race shots 10700 / 11500 changed in 73.7 % / 73.1 % of their pixels, all less green); golden compare without mods: 140 PASS (rate: no rates, RATE_RUNS=0) |
| **4 (done, tune4)** | the tuning console (registry wrapper, DEVELOPER page, F9 overlay, `tune` events, `[tuning]` presets), §4.1; its DEVELOPER MODS switch is the same Developer mods as 2b's platform hooks (one `pc_settings.ini` value; the launcher Mods page's button and `--developer-mods` set it), and its wrappers go in before the code and script mods' (port layer first) | with the console off: golden PASS; an edited value replays exactly from the recording; a preset mod is flagged gameplay-changing. **Checked** (tune4): golden compare PASS with the console off; `tools/modding/tuning_check.sh` PASS (the registry counted in an offscreen race, 1088 AddEntry calls, with the console on and RAM / shots equal to the golden race run; a preset mod flagged and its value written as the camera registered, the race shot 11500 changed, the intro shot equal; a `--tune` edit replayed from its pad recording and from its replay file with every RAM hash, shot and edit line equal, 39 of 39 checkpoints); `make test` (test_tune); Windows cross build |
| 5 | the docs of §5 (each phase adds its pages; tutorials once their tools exist). **file-formats/ done** (moddocs1): 14 pages, the tuning registry among them. **README.md, tutorials 01 and 02 done** (tutorials1; checked by their author and by tutorial_check.sh) | every tutorial followed from a fresh checkout on one's own disc by someone who did not write it; file-formats: VDB.XML's layout checked on the disc (`tools/modding/vdb_read.py`) |
| **6 (done, lua6)** | Lua scripting (§10, as built §10.1) on the SDK of 2b: a sandboxed, deterministic Lua 5.4 per mod, the same groups as the C ABI, hot reload of scripts (with Developer mods, the switch shared with part 4); a script mod may also carry `[files]` (files and patches), a `[textures]` pack and a `[tuning]` preset, left out with it when it does not load | golden without mods PASS; a script mod's replay equal; a script cannot reach the host file system or the network; a runaway script is stopped at its instruction budget. **PASS** (2026-11-12): golden compare without mods every item PASS but the rates (none recorded: RATE_RUNS=0); `examples/mods/lua-speedo` in race_check STRICT PASS, its box with the speed (87, the game's own speedometer 87 mph) and the race time in the race shots, and `replay_check.sh` PASS (39 of 39 checkpoints; trace, DMA, SIF, RW Audio, RAM and shots equal); a test script looping forever at VBlank 21800 stopped by its budget, the race going on to PASS and its drawing gone from the later shots; hot reload in a race with Developer mods (the edited box colour in the next shot); `make test` (test_script_mods: the sandbox's refusals, budget, memory cap, seeded random, the API on synthetic memory); Windows cross build. Not seen in a game run yet: the `lap` and `finish` events (the checks' drives end before a lap line; unit-tested) |
| **7 (cars and routes in Blender: done, blender1 / blender2)** | Blender add-on (§11) on 3c's formats: import / export of cars and routes. **Cars** (§11.1): B3 Car Tools, a front end on b3modtool (import from the disc, Check, export to a patch mod / .b3mod / car file), tutorial 09. **Routes**: Blender's own glTF importer and exporter with the options of tutorial 10 (no add-on needed: `track import` takes the result) | a car exported, imported into Blender, exported back and imported by b3modtool gives the same bytes; a recoloured, reshaped car in a race shot. **Checked** (2026-11-13, blender2) in Blender 4.2.23 LTS, headless: `tools/modding/blender_check.sh` PASS with NO_RACE=1 six times (PYTHONHASHSEED 1 .. 5 and unset): the extension validates, the add-on installs and enables, Compact Type 1, Classic Hotrod and Fire Truck imported and round-tripped byte for byte, Compact Type 1's LOD 3 body and panels 1.3x in object mode exported as a mod (25 meshes edited in place, the same size, verify OK); US/C2_V2 through Blender unedited: the disc's STATIC.DAT and STREAMED.DAT (2,335 of 2,335 meshes unchanged, no patch), with the green taken out of its 37 sections' colours and section 0 lifted 0.5 m: 1,360 meshes edited in place, 0 with new triangles, the sizes kept, verify OK. With the races: PASS (race_check PASS without a mod, with the car mod and with the track mod, all with the same 11,679,530 VU0 launches; the game read `PVEH/COMP/CAR1.BGV` and `TRACKS/US/C2_V2/STREAMED.DAT` from the mods' patches; race shots 10700 and 11500 changed with each). Every route through Blender unedited: 37 of 37 byte for byte. The bug found on the way: Blender's importer pairs a mesh's `_` attributes with their data in a set's order (string hashing), so the earlier VEC4 / VEC2 / SCALAR mix failed in 4 of 6 runs; the exports now write one float SCALAR per component (§11.1). Also `make test` (test_b3_car_tools.py, test_modtool: both attribute layouts) and `model_import_check` ("through Blender": 67 / 67 identical, 67 / 67 scaled in place, 67 / 67 old-layout exports identical by the import and refused by the modelled importer) |
| 8 | content support (§12): new car slots, liveries, events, crash junctions, traffic sets, HUD layouts | each with a neutral example mod and its race_check; golden without mods PASS |
| 9 | developer tools (§13) and publishing (§14) | each tool off by default; golden PASS; `b3modtool verify --ci` used by the example mods' own check |

**Recommended order after Phase 1:** 2a (small, no behaviour change, and every later code-facing part needs one hook
table) -> 3a (textures and text are what most modders want first, and they need no code) -> 1b (the renderer readers,
needed as soon as 3c or track mods exist; zip install for users) -> 2b (the SDK) -> 4 (the console sits on 2a / 2b) ->
3b / 3c -> 6 (Lua, once the C ABI is stable) -> 7 (Blender, once 3c's formats round-trip) -> 8 -> 9. Part 5 grows
with each phase.

## 10. Lua scripting

Most modders do not write C. A script layer over the C ABI of §2 lets them make modes, rules, HUDs and challenges.

- **Language and runtime.** Lua 5.4 (MIT), built into native_play, one state per script mod. `[script] main =
  scripts/main.lua` in mod.ini. Scripts are data: no compiler, the same file on every platform.
- **Sandbox.** No `io`, `os`, `package.loadlib`, `debug` or `require` outside the mod's folder. Files only through
  `b3.data.open` (the overlay, read-only) and the mod's own folder. No network. An instruction budget per VBlank
  stops a runaway script with a message; the game goes on without it.
- **Determinism.** Scripts run on the game thread at VBlank boundaries, with no wall clock (`b3.vblank()` is the
  clock), a seeded `math.random` per mod, and table iteration that never depends on addresses. Floats are host
  doubles inside the script; values written into the game go through the same bounds-checked writes as §2.2. So a
  script mod replays exactly from a pad recording, and its id, version and script hashes are in the mod set hash.
- **API.** The groups of §2.2 as Lua tables: `b3.log`, `b3.on_vblank` / `on_frame` / game events (race start, lap,
  crash, takedown, finish: wraps the SDK installs on the game's own functions), `b3.mem.read` / `write`, `b3.call`
  (game functions by address, through the hook table), `b3.settings.add`, `b3.draw.quad` / `text` / `texture`.
  A documented set of the game's objects (the race, the player's car, the score, the event) as read-only views
  with named fields, so a script never needs raw offsets for the common things.
- **Hot reload.** In developer mode a changed script is reloaded at the next VBlank (its state rebuilt by its
  `on_load`); never in a normal run, so a run is always one version of each script.
- **Gameplay flag.** A script that writes game memory or replaces a function is gameplay-changing (§6) unless the
  manifest says `gameplay = no` and the script only draws and reads; the loader enforces this by refusing writes
  from a `gameplay = no` script.

### 10.1 As built (Phase 6, lua6)

The reference is [lua-reference.md](lua-reference.md); where it differs from the plan above, this section counts.

- **Lua 5.4.9**, vendored unmodified under `src/third_party/lua-5.4.9/` (`src/`, README, `doc/readme.html`, the MIT
  notice as `LICENSE.txt`; the tarball's SHA-256 in `src/third_party/lua-5.4.9.tar.gz.sha256`, checked against Lua's
  download page and Debian's source package; `src/third_party/README.md`), in the release notices
  (`tools/release/THIRD-PARTY-NOTICES*.txt`, `Lua-LICENSE.txt` in both packages). It is compiled **as C++ in one unit**
  (`src/native/script_lua.cpp`: core, auxiliary library, base / coroutine / math / string / table / utf8 only; io, os,
  package, debug are not in the program), so a Lua error unwinds as a C++ exception through the port's frames; the same
  unit builds in native_play on every platform (Windows: `tools/native/build_win.sh` takes it from `NATIVE_SRC`).
  Two macros make it deterministic and locale-free: `luai_makeseed` (the string-hash seed) is a constant, and the
  decimal point is always `.`.
- **The manifest**: `[script] main = scripts/main.lua` (a `.lua` file in a folder of the mod), `gameplay`, `budget`
  (instructions per VBlank), `memory` (MB); `api = 3` (`b3mod::kApiVersion` 3, `kScriptApi`); a mod has `[code]` or
  `[script]`, not both. Every file of the script folder is in the mod hash (`script <path> <size> <fnv>` lines after the
  `code` lines, before a `[tuning]` preset's line; the set hash's text keeps `b3mods api 1`). `include/b3mod/mods.h` ScriptSpec, `src/b3mod/mods.cpp`.
- **On the code-mod loader.** A script mod is a code mod whose entry point is in native_play
  (`src/native/script_mods.h`): `CodeMods::Candidate::builtin` (a `CodeMods::Builtin` with `main(api, self)`) stands where
  a library would, so the script loads at the code mods' point and order, all or nothing, and everything it registers is
  the B3ModApi's: hooks (`mod:<id>`), rows (the MODS tab, the Mods page, recordings), overlay items, log lines, file
  reads, mods-state.ini. native_play adds the candidates (`script_candidate`) and nothing else; the launcher's Mods page
  shows a script card (what it is, gameplay, the last start, its rows).
- **One lua_State per mod** with its own allocator (the memory cap; a refused allocation is Lua's memory error) and a
  count hook every 1,000 instructions (the budget: per VBlank, the main chunk and `on_load` 50 times it). modsec: and a
  wall-clock limit on the script's own time per VBlank (1 s; loading 10 s; per script, paused while the game's code it
  calls runs, the running script set at every entry), looked at in the count hook, the allocator
  and each step of lstrlib's pattern matcher (script_lua.cpp redefines `l_unlikely` around lstrlib.c only, so each of
  its error checks also asks; the vendored file unchanged); `string.rep` capped at the memory cap, `""` at once. Every entry
  from the game (the main chunk, a VBlank, a frame, a hook, a wrapped event, shutdown) is one protected call; an error,
  the budget or the cap there **stops** the script: logged with the message and its traceback, its callbacks no longer
  called, its hooks passing each call to the game's function with the game's arguments, its state closed. A `pcall` in
  the script cannot keep a stopped script running (past the budget every instruction raises the error again).
- **Sandbox**: no io / os / package / debug, no dofile / loadfile / string.dump; `load` takes text only; `require`
  reads `<script folder>/<name>.lua` only; `b3.data` reads the disc through the overlay and the mod's own folder through
  the API's `data_size` / `read_data` (no other path); no clock (`b3.vblank()`); no network.
- **Determinism**: the state runs only on the game thread at the fixed points above; `math.random` is seeded with
  FNV-1a 64 of the mod id and `math.randomseed()` without a seed is refused; string keys iterate in a fixed order
  (constant hash seed); tables keyed by tables or functions do not (documented: an order to keep out of game effects);
  game code is called only from the state's main thread (not from a coroutine), so a hook it reaches enters the state
  where it is.
- **The API** (`b3`): log / warn / vblank / id / version / gameplay / developer; on_load / on_vblank / on_frame /
  on_shutdown; `b3.on` game events: `crash` (wrap of CB3RaceCar::StartCrashing 0x001519F0), `takedown` (wrap of
  CB3Score::OnTakeDown 0x001553F0, fired when the score's count +0x68 grew), `finish` (wrap of CB3RaceCar::SetFinished
  0x00151780) -- names from the decomp's symbol file, untagged [C] -- and `race_start` / `lap`, read at each VBlank from
  the player's CB3Score (state +0x27C becoming 2, laps done +0x18 growing; capture.h ScoreState [C]); the lap function
  itself (func_0015A1F0) has only a guessed name, so it is not wrapped. `b3.mem` read / write (u8 .. u32 signed and
  unsigned, f32, bytes), `b3.call`, `b3.hook.wrap` / `replace` (a `call` object: arg / farg / set_arg / next / result /
  set_result), `b3.settings` add / get / index, `b3.draw` quad / text / texture, `b3.data` size / read / open, and the
  views `b3.view.race()`, `player()` (position: the race car's matrix; speed: the speedometer's source vector
  0x0055EF70 + 0x1CB80 + 0xB0, [L] from CB3HUDSpeedo::Update's nonmatching but logic-exact source), `score()` (capture.h
  ScoreState's fields, takedowns +0x68 [L]).
- **unsafe** (modsec, 1.3.0): `mem.write*`, `b3.call`, `hook.replace` and a call's integer `set_arg` / `set_result`
  hand the game's native code values the script chose (a pointer, an index, a skipped function's v0), which can reach
  past the sandbox, so they need `[script] unsafe = yes` (with `gameplay = yes`; read from the hashed mod.ini) and
  `b3.call` only reaches the addresses `[script] calls` lists; `hook.wrap`, `set_farg` and `set_fresult` stay with
  `gameplay`. A hot reload (Developer mods) runs the edited files unasked. Such a mod needs the player's permission as a code
  mod does (trust over mod.ini and every file of the script folder, b3mod::trust_files), and the runtime runs the
  bytes it read and hashed at load. A native function's pointer argument is range-checked for its pointee's whole
  size (b3host call.h `from_image`), not its first byte. A fatal error of the host (b3host::HostError) in the game's
  code a script called (`b3.call`, `call:next`) is caught in that binding, never by Lua (Lua compiled as C++ would
  turn it into a script error a `pcall` catches: a probe, and a half-run game function going on): the script is
  stopped (every later instruction raises), and once its state has unwound the error is thrown on to the game.
  Every C function the script can call is wrapped the same way (`guarded<>`), so a HostError from a view, a read or a
  draw is never a Lua error either. The vendored Lua is not changed.
- **gameplay = no** refuses `mem.write*`, `b3.call`, `hook.replace` and a call's `set_*` (each an error); not given:
  read as yes, with a note (the Mods page then says it changes gameplay).
- **Hot reload** only with Developer mods (the one switch of §2 and §4.1: `pc_settings.ini` developer_mods, or
  `--developer-mods`): the script folder is looked at every 30 VBlanks (names, sizes, times); a
  change loads the script again at that VBlank in a new state (main chunk, `on_load(true)`); the first load's hooks,
  rows and wrapped events stay, a new script's hooks on the same addresses take them over. Logged as a run that no
  longer plays back as recorded.
- **The overlay's dots**: the stroke font writes `.` and `:` as zero-length strokes, which the renderer's mod overlay
  skipped (`TIME 0 08 98`); it now draws each as a square (`src/b3replay/modoverlay.cpp`), for code mods too.
- **Checks**: `tests/test_script_mods.cpp` (make test); the example `examples/mods/lua-speedo` and
  [tutorial 06](tutorials/06-a-lua-script.md) (§9 row 6 has the runs).

## 11. Blender add-on

Cars and tracks need a modelling tool. `tools/modding/blender/b3_blender` (Python, Blender 4.x, GPL-compatible as
Blender's add-ons must be) on the formats b3modtool round-trips (§3, Phase 3c):

- **Cars**: import a car exported by `b3modtool car export` (its LODs, damage-deformation data, wheels, lights,
  materials and textures as Blender objects and collections, named the game's way); export back to the same
  intermediate format, which `b3modtool car import` turns into the game's file. Checks in the add-on before export:
  vertex and strip limits per packet, texture sizes and formats the game accepts, the required named parts.
- **Routes**: import a route's meshes, materials and textures by section, edit, export; collision and AI paths
  later, once their formats are documented (docs/track-format.md).
- **Acceptance**: export -> import -> export with no edit gives identical files; a reshaped car drawn in a race shot.

### 11.1 As built (cars, blender1)

The add-on is `tools/blender/b3_car_tools` (not `tools/modding/blender/b3_blender` as planned above), MIT like the rest
of the port's tools (GPL-compatible), Blender 4.2 LTS and newer, installable as an extension (`blender_manifest.toml`)
or a legacy add-on (`bl_info`) from one zip (`make blender-addon`: `build/b3_car_tools.zip`; the Windows and Linux
packages carry it beside b3modtool). It is a **front end on b3modtool**: no file format is read or written in Python.

- **Import** (File > Import > Burnout 3 Car): `b3modtool car list` for the names, `car export` into a fresh folder of
  the work folder (never over an earlier export: its paint PNGs may hold edits), Blender's own glTF importer into a
  collection of the car's own (merge vertices off, custom normals, textures not packed). The root empty keeps the
  sidecar and the export's place as the custom property `b3_car` (`props.py`), so a saved .blend finds its files, and a
  lost sidecar is written back from it.
- **Check** / **Export to Mod** (the sidebar's B3 Car tab; File > Export > Burnout 3 Car Mod): paint pictures painted on
  in Blender saved, the car's collection through Blender's glTF exporter as a GLB beside the sidecar (the options in
  `gltf_settings.py`: `_` attributes and extras on, Draco and shared accessors off, placeholder materials, only the
  collection; options a Blender lacks are left out, with a warning where it matters), then `b3modtool car check`
  (new: the import without writing) or `car import` into a mod folder, `verify --game`, and `pack` for a .b3mod.
  b3modtool's refusals become Blender reports with what to do (`errors.py`; the objects they name selected).
- **The b3modtool side** (what Blender does to a car, from glTF-Blender-IO's source and run in Blender 4.2.23 LTS:
  b3modtool.md "Blender"): car exports write the stored fields as one float SCALAR per component (`_B3_NRM_X` .. `_Z`,
  `_B3_INFL_0` / `_1`, `_B3_W`). Floats because Blender keeps only float attributes as they are (an unsigned short VEC2
  was dropped and an unsigned byte VEC4 clamped as a colour); one form because its importer
  (`io_scene_gltf2/blender/imp/gltf2_blender_mesh.py`) collects a mesh's `_` attribute names in a Python set and its
  data buffers in a list in the order first seen, then pairs them by the set's iteration order, which string hashing
  changes from run to run: the earlier VEC4 / VEC2 / SCALAR mix failed with a numpy `ValueError` in 4 of 6 runs (seeds
  1 .. 6). The imports read both layouts. The import takes a missing influence from the nearest original vertex by the
  mesh's own positions with the texture coordinates as the tie-break, leaves out influences that are not the file's
  and stored normals that no longer match `NORMAL`, and says so in one note per kind. The add-on moves the objects
  Blender's importer made into the car's collection (4.2 links them to the scene's own collection, not the active
  one).
- **Checks**: `tools/blender/test_b3_car_tools.py` (in `make test`), `model_import_check`'s "through Blender",
  `tools/modding/blender_check.sh` (§9 row 7: run in Blender 4.2.23 LTS).

## 12. Content support specific to the game

What players will want to make, each with a neutral example mod once its phase lands:

- **Cars**: replacing a car (3c), then **new car slots** (code: the car list, the unlock tables and the garage
  pages extended by a hook; ids recorded in the mod set). Liveries as texture mods with per-car previews.
- **Routes and events**: new routes on the existing track slots, then new event entries (World Tour events, crash
  junctions with their own scoring targets, time attacks) through the event tables the decomp documents.
- **Traffic and AI**: traffic sets (which vehicles, how dense), AI difficulty presets through the tuning console.
- **Presentation**: HUD layouts (script or code overlays, the game's HUD hidden piece by piece), cameras (chase
  camera parameters, new views), menu text and translations (strings), loading screens.
- **Music**: a mod may bring its own music through the custom soundtrack feature (remaster-features §6), only music
  its author has the rights to. The kit ships no third-party mods, and the docs name none.

## 13. Developer tools (developer mode only, off by default)

A free camera (the photo mode's, unlocked from the race), collision / trigger / AI-path views drawn by the renderer,
frame step and slow motion, a guest-memory inspector with the documented structures by name, a function call tracer
on the hook table (counts and arguments for chosen addresses), and hot reload of overlay files (a changed texture or
string file served from the next time the game opens it). Documented structure layouts (the race, cars, score,
events, tuning registry) as an SDK header and as pages in `docs/modding/`, from the port's own names layer.

## 14. Publishing

The `.b3mod` package (a zip with mod.ini at its root) and the manifest schema are versioned and documented
(`mod-format.md`). `b3modtool verify --ci` gives a machine-readable verdict a mod author can run in their own CI:
the manifest, paths, sizes, no game file inside, the patches' source hashes. Mods are shared where the community
already shares them; the launcher's "Add mod" takes a `.b3mod` file or a folder. No mod hosting by the port itself.
