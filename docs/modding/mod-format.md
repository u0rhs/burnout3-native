# Mod format

The files a mod is made of and the files the port keeps about mods, as the port reads them today (mod API 3: data mods,
with `[code]` code mods, with `[script]` script mods). How to
install and make a mod: [getting-started.md](getting-started.md). The plan behind it: [DESIGN.md](DESIGN.md) §1. Code:
`include/b3mod/mods.h`, `include/b3mod/archive.h`, `include/b3mod/codemod.h`, `include/b3asset/hdpack.h` (texture packs).

## A mod folder

```
<user data folder>/mods/<folder>/
    mod.ini          the manifest (required)
    data/            the files that stand in for the disc's, at their disc paths ([files] root)
        DATA/NEWFILE.BIN
    patches/         changes to the disc's files, at their disc paths plus .b3patch ([files] patches)
        DATA/FRONTEND.TXD.b3patch
        TRACKS/US/C2_V2/STATIC.DAT.b3patch
    textures/        HD replacement textures for the renderer ([textures] pack, optional)
    bin/linux/x.so   a code mod's library per system ([code]; bin/windows/x.dll, bin/macos/x.dylib)
    scripts/main.lua a script mod's Lua scripts ([script] main and its folder; a mod has [code] or [script])
    tuning/x.ini     a tuning preset ([tuning] preset, optional)
    README.txt       anything else is ignored by the port (a README and a LICENSE are good to have)
```

A mod may have any of the three data folders, and a code mod its libraries or a script mod its scripts beside them. A
mod meant for sharing changes the disc's files with patches (they hold only what the mod changes, never the game's own
bytes); a whole file in `data/` is for files the mod makes itself, or for trying a change on your own PC.

The folder's name does not matter (the id in `mod.ini` names the mod). A folder whose name starts with `.` is skipped.

## mod.ini

A small ini file: `[section]` lines, `key = value` lines, comments starting with `#` or `;` (a whole line, or after a
value). A value in double quotes keeps its spaces. Section and key names are not case-sensitive. A key given twice:
the first one counts (with a note). Lines the port does not understand give a note, never an error, unless the table
below says so.

### [mod]

| Key | Required | Meaning |
|---|---|---|
| `id` | yes | The mod's name in `mods.ini`, in `native_play --mods`, in `[requires]` and in the logs. `a-z`, `0-9`, `.`, `_`, `-`; 1 to 64 characters; not starting with `.`. Two installed folders with the same id: the second cannot be used. |
| `name` | no | Shown on the launcher's Mods page. Default: the id. |
| `version` | no | Any text without spaces, e.g. `1.2.0`. Default `0` (with a note). Part of the set hash; `[requires]` compares it (below). |
| `author` | no | Shown on the Mods page. |
| `description` | no | One line, shown on the Mods page. |
| `api` | no | The mod API version the mod was made for. The port's is 3 (1: data mods; 2: also code mods, `[code]`; 3: also script mods, `[script]`). Missing: read as 1, with a note. Higher than the port's: the mod cannot be used ("a newer port is needed"). A mod with `[code]` needs 2 or more, a mod with `[script]` 3. |
| `priority` | no | A whole number, default 0. Only used when the mod is first installed: it joins the load order above the mods with a lower priority. |

### [files]

| Key | Meaning |
|---|---|
| `root` | The folder inside the mod that holds the disc paths. Default `data`. Must be a relative path inside the mod: no `..`, no absolute path, no drive letter, no link that leads out (otherwise the mod cannot be used). |
| `patches` | The folder inside the mod that holds `.b3patch` files, each at its disc path plus `.b3patch` (below). Default `patches`. The same rules as `root`. |

Paths below the root are the disc's paths. Upper and lower case do not matter (`data/data/frontend.txd` stands in for
`DATA\FRONTEND.TXD`); a mod may also add paths the disc does not have. Left out, with a note in the log:

- `SLUS_210.50` and `SYSTEM.CNF` (the port runs the game's code natively; behaviour changes need a code mod);
- two files whose names differ only in case;
- a file reached through a link that leads out of the mod's folder;
- files larger than 4 GB;
- dot files (`.DS_Store` and the like), `Thumbs.db`, `desktop.ini`.

#### Patches

`patches/DATA/FRONTEND.TXD.b3patch` changes the disc's `DATA/FRONTEND.TXD` ([b3patch.md](b3patch.md); made with
`b3modtool diff`). When the game starts (and when the launcher's Mods page is built) the port:

1. reads the player's own `DATA/FRONTEND.TXD` from their disc and checks it against the size and FNV-1a 64 hash the
   patch names as its source;
2. applies the patch and checks the result against the patch's target size and hash;
3. keeps the result in the cache, `<mods folder>/.cache/<patch hash>-<source hash>.bin` (16 hex digits each), and
   serves it as if the mod had that file in `data/`: to the game, to the renderer's own readers and to the launcher's
   art. The next start finds the file there (checked by its size and hash) and does not apply the patch again. The
   cache folder is safe to delete at any time; nothing else is written, not to the disc and not to the mod.

The log says `mods: DATA/FRONTEND.TXD from <id> (8517568 bytes, its patch applied to your disc's file)` the first
time the game opens the file.

**A patch that does not fit the player's disc** (made from another version or region of the game, or from a file
that was already modified), a damaged patch, a patch for a file the disc does not have, or a patch and a file in
`data/` for the same path: **the whole mod is left out** of the run, with the reason in the log and on the Mods page:

```
native_play: mods: blue-logo: patches/DATA/FRONTEND.TXD.b3patch was made from another version of DATA/FRONTEND.TXD than
your disc's (it needs 8517568 bytes, FNV-1a ...; yours is ... bytes, ...): another region or version of the game, or a
modified file: the mod is off for this run
```

The mods that require it are left out too. As for any mod that cannot be used, a run that names its mods
(`native_play --mods`) stops (exit 2) instead of starting without it. Patches apply to the disc's own files only: a
patch is never applied on top of another mod's file, so two mods that patch the same file are a conflict like two
mods with that file (the higher one in the list wins).

### [textures]

| Key | Meaning |
|---|---|
| `pack` | A folder inside the mod holding HD replacement textures in the PCSX2 format: DDS files named `<texture hash>-<palette hash>-<bits>.dds` (or `<texture hash>-<bits>.dds`), in any sub-folders. No default: a mod without this key has no textures. The same rules as `root`. |

The format is the one the port's HD pack setting reads (docs/native-renderer.md §18): a replacement made for PCSX2's
texture replacement of this game works unchanged. The renderer reads the textures (the game never does). **Order:**
the textures of the mods that are on come first, in the load order (the higher mod's wins when two replace the same
texture), then the player's own HD pack (PC SETTINGS / the launcher's HD pack folder), so a mod's texture is drawn
over the player's pack's for that texture, and the player's pack still supplies every texture no mod replaces. A
mod's textures follow its on / off switch: they are drawn when the mod is on even if the player's own pack is off or
not set. With no mod that has `[textures]`, nothing changes. The textures are not part of the set hash (below): they
never change what the game does.

### [requires]

Other mods this mod needs. One line per mod: its id, and the oldest version that works (empty or `*`: any version).

```ini
[requires]
base-pack = 1.2      # base-pack 1.2 or newer
other-mod =          # any version
```

- A requirement that is not installed, not on, cannot be used, or older than the version given: the mod is left out
  of the run, with the reason in the log (`native_play: mods: <id>: it requires ...: left out`) and on its card on the
  Mods page ("Off at the next start: ..."). Leaving it out can leave out the mods that require it in turn. When
  `native_play --mods` names the set (a script), any mod left out ends the run (exit 2), as for a missing id.
- Versions are compared part by part, split at `.`: a part that is a number by its value (`1.10` is newer than `1.9`),
  other parts as text, a number before text; a missing part counts as 0 (`1.2` and `1.2.0` are equal).
- **Load order.** A requirement does not move a mod in the list: the list alone decides which mod's file is used
  when two have the same path (the higher one). A mod that changes files of a mod it requires should be placed above
  it; when it is below, the log and its card say so.
- An id that is not a valid id, a version with spaces, or a mod requiring itself: the mod cannot be used.

### [code]

A code mod: a library the game loads and runs ([sdk-reference.md](sdk-reference.md); the mod needs `api = 2`).

```ini
[code]
windows = bin/windows/my_mod.dll
linux = bin/linux/my_mod.so
macos = bin/macos/my_mod.dylib
gameplay = no
platform = no
```

| Key | Meaning |
|---|---|
| `windows`, `linux`, `macos` | The library for that system, a file inside the mod (no `..`, no absolute path). At least one; the game loads its own system's. |
| `gameplay` | `yes` / `no` (default `no`): the author's statement that the mod changes physics, AI or scoring. Shown on the Mods page. |
| `platform` | `yes` / `no` (default `no`): the mod hooks the platform tier; allowed only while Developer mods is on (PC SETTINGS > INTERFACE > DEVELOPER MODS, or the launcher Mods page's button: one switch). |

A `[code]` with `api` 1, without a library, or with a path that leaves the mod: the mod cannot be used. A library that
is not there for the system the game runs on: a note in the log; the mod is not loaded there. The game loads a code
mod only after the user allowed its library in the launcher (asked once per library); a code mod that does not load
is left out of the run with its files: its `data/` files and its patches (the game's set is rebuilt without it, the
other mods' patched files coming from the cache) and its `[textures]` pack (unless the renderer had already opened
its HD textures by then: the pack then stays until the next run, with a line in the log; it changes only the
picture) and its `[tuning]` preset.

### [tuning]

A tuning preset: values for the game's tuning variables (boost and score rules, AI, car physics, cameras, sound
mixing, rumble), applied as the game registers them. The file's format and when it applies: [tuning.md](tuning.md).

| Key | Meaning |
|---|---|
| `preset` | The preset file, a relative path inside the mod (e.g. `tuning/handling.ini`). No `..`, no absolute path, no link that leads out; the file must be there. Otherwise the mod cannot be used. |

A mod with a preset **changes gameplay**, whatever else it does: the Mods page says so on its card, native_play's log
says so at the start (`native_play: tuning: preset of <id> ...: this mod changes gameplay`), pad recordings get a
`gameplay tuning <ids>` line and replay files a `gameplay` entry in their notes. The preset's bytes are part of the
mod's hash (below), so a replay made with one preset is refused with another.

### [script]

A script mod: a Lua 5.4 script the game runs ([lua-reference.md](lua-reference.md); the mod needs `api = 3`).

```ini
[script]
main = scripts/main.lua
gameplay = no
budget = 1000000
memory = 64
```

| Key | Meaning |
|---|---|
| `main` | The script, a `.lua` file in a folder inside the mod (`/` between folders; no `..`, no absolute path; not at the mod's top). Required. Its folder is the script folder: `require` reads the other scripts there, and all its files are in the mod hash. |
| `gameplay` | `yes` / `no`. `no`: the script only reads and draws; the port refuses its writes to the game, its calls of game functions and its replacements. `yes`, or not given (with a note): it may change the game, and the Mods page says it changes gameplay. |
| `budget` | Lua instructions per VBlank before the script is stopped: 10000 to 50000000, default 1000000. |
| `memory` | Megabytes of Lua memory before the script is stopped: 1 to 512, default 64. |

A mod has `[code]` or `[script]`, not both. A `[script]` with `api` below 3, without `main`, or with a `main` that is
not a `.lua` file in a folder of the mod: the mod cannot be used. A `main` that is not there: a note in the log, and the
mod is not loaded. Scripts need no permission in the launcher (they run inside the game through the script API only);
a script mod that does not load is left out of the run with its files, as a code mod.

### Sections for later parts of the kit

Any other section is not used by this port yet: a note "not used by this port", never an error, so a mod made for a
newer port still loads its data files here when its `api` allows it.

## mods.ini (the load order)

`<user data folder>/mods.ini`, written by the launcher's Mods page:

```ini
[order]
patch-for-base = on     # first line: the highest priority
base-pack = on
other-mod = off
```

Of the mods that are on, the first one in this list that has a path supplies it. `on`, `1`, `true`, `yes` mean on;
anything else is off. Ids that are not installed are dropped when the launcher saves; mods that are installed but not
listed join at the end, off (the higher `priority` first, then by id).

**Developer mods** (code mods with `platform = yes` may then hook the platform tier; the tuning console is on) is not
kept here: it is `developer_mods` in `pc_settings.ini`, which PC SETTINGS > INTERFACE > DEVELOPER MODS and the Mods
page's **Developer mods** button both set. An `[options]` section in `mods.ini` (written by test builds before that) is
ignored.

## The files the port keeps about code and script mods

In the user data folder, beside `mods.ini` (window runs only; offscreen runs read and write none of them):

| File | Written by | Holds |
|---|---|---|
| `mod-settings/<id>.ini` | the game (a change on PC SETTINGS > MODS) and the launcher's Mods page | a code or script mod's settings values: `[settings]`, then `<key> = <value>` (`Off` / `On`, a choice's name, a slider's level) |
| `mods-trusted.ini` | the launcher (when the user allows a mod's code) | `[trusted]`, then `<SHA-256 of the library> = <id> <version>`; delete a line to be asked again |
| `mods-state.ini` | the game, at each window start with code or script mods | per code or script mod (`[mod <id>]`): `status` (loaded / not loaded), `reason`, `library` (its SHA-256), `row` lines (its settings rows); the Mods page shows it |

## Archives (.b3mod, .zip)

A `.b3mod` file is a zip archive; the launcher's **Add mod...** installs either. The archive holds the mod folder's
contents: `mod.ini` at the top of the archive, or in its one top folder (an archive made by zipping the mod's folder;
that folder is not kept). A `__MACOSX` folder (made by macOS's Finder) is ignored. The mod is unpacked into
`mods/<id>/`.

The archive is checked before anything is written, and refused as a whole when:

- an entry's name would land outside the mod's folder or is not portable: an absolute path, a drive letter or any
  `:`, a `.` or `..` component, an empty component, control characters or `< > " | ? *`, a name ending in `.` or a
  space, a Windows device name (`CON`, `NUL`, `COM1` ...), a name that is not UTF-8;
- an entry is a link (Unix symbolic link, Windows reparse point), is encrypted, or is compressed other than stored or
  deflate; the archive is ZIP64 or split into parts;
- an entry is larger than 2 GB, the entries add up to more than 16 GB, or there are more than 100,000 of them; an
  entry's data is longer than its header says, or its CRC does not match;
- two entries have names that differ only in case, or a name is both a file and a folder;
- there is no `mod.ini` at the top (or in the one top folder), or it has errors;
- a mod with the same id is installed already (the launcher then offers **Replace**: the old folder is replaced, the
  mod keeps its place and its on / off switch), or the mods folder has a folder named like the id that is not that mod.

Unpacking goes into a hidden folder (`.install-<id>`) that is renamed into place at the end, so a failed install leaves
nothing behind. A new mod starts off.

## What the port computes from a set of mods

**The set hash** names exactly the bytes the game can read and the order that decides who wins (FNV-1a 64 over text,
so any tool can recompute it):

- per mod: over `file <KEY> <size> <fnv64 of the bytes, 16 hex>\n` for each file in `data/`, and
  `patch <KEY> <size of the .b3patch> <fnv64 of the .b3patch's bytes, 16 hex>\n` for each patch, sorted by key (the
  key is the disc path in upper case with `/`), then for a code mod `code <system> <path> <size> <fnv64, 16 hex>\n`
  for each library `[code]` names, in the order windows, linux, macos (`code <system> <path> missing\n` when the file
  is not there), so the same mod folder hashes the same on every system. A patch names the hashes of its source and
  its result, so a patch mod gives the same hash on every machine whose disc it fits; the cache does not enter it.
  `[textures]` files do not either (the game never reads them). Then for a script mod (lua6) `script <path> <size>
  <fnv64, 16 hex>\n` for each file of the script folder (the folder of `[script] main`, every file below it but dot
  files, `Thumbs.db` and `desktop.ini`), sorted by its path in the mod with `/` (`scripts/lib/util.lua`), and
  `script <main> missing\n` when `main` is not there. Then, for a mod with a `[tuning]` preset (tune4),
  `tuning <preset path as in mod.ini> <size> <fnv64, 16 hex>\n`. In that order: files and patches, code lines, script
  lines, the tuning line;
- the set: over `b3mods api 1\n` (the hash text's own version, unchanged by api 2), then `mod <id> <version> <mod hash,
  16 hex>\n` per active mod in priority order.

A code or script mod's settings values are not in the hash: a pad recording or replay records them (`set` lines, `mod.<id>.<key>`
settings), and a replay applies them.

native_play prints it at the start of every run (`native_play: mods: active (...): ...; set hash <hex>`, or
`native_play: mods: none`), writes it into pad recordings (`mods <hash> <ids>`), and hands the set to the renderer.

**File conflicts.** A path that more than one active mod has: the first in the list wins, the others' files for that
path are hidden. The Mods page lists, per mod that is on, the paths where it wins over another mod and the paths
where a mod above hides its file, and counts them at the top.

**The renderer's copy.** The renderer reads some disc files itself (it finds the route by the STATIC.DAT the game
loaded, loads whole tracks for draw distance, scans the cars, reads textures). native_play passes it the set
(`--mods-dir <folder> --mods <ids> --mods-hash <hash>`, from `b3mod::renderer_args`); the renderer builds the same
overlay (the patched files from the same cache), checks the hash (a warning in the log when a mod's files changed in
between), reads those files through it, and puts the mods' `[textures]` packs above the player's HD pack. When a code
mod does not load, native_play passes the narrowed set again and the renderer rebuilds its overlay the same way. So the
game and the picture always use the same files. The launcher's own art (its logo and fonts' textures from
`DATA/GLOBAL.TXD` and `DATA/FRONTEND.TXD`) also comes through the mods that are on, patches applied.

**Safe mode** (after two starts in a row that did not end cleanly, or with `--safe-mode` / the launcher's "start in
safe mode") starts without any mods, `--mods` included; the log says `native_play: mods: SAFE MODE (...): this run
starts without mods`, and the window's safe-mode notice adds a line. The next normal start uses them again.
