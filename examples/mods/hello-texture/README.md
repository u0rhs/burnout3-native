# hello-texture: replace one texture

The mod kit's first example, the folder of [tutorial 01](../../../docs/modding/tutorials/01-replace-a-texture.md). It
shows the shape of a data mod: a manifest (`mod.ini`) and a `data` folder holding files at their disc paths. **It
contains no game data**, so as shipped it changes nothing (the launcher notes "no data folder"); you make its one file
from your own disc.

The texture: the title screen's logo, raster 203 `titlescreen_logo` (512 x 256, 256 colours) in `DATA/FRONTEND.TXD`.

## Make it

With [`b3modtool`](../../../docs/modding/b3modtool.md) (in the release beside the launcher, or `make modtool`), from
the folder that holds `hello-texture` (keep the exported pictures, `work/`, outside the mod folder: they are the game's
pictures and must not end up in the mod you share); `<disc>` is your disc folder or image:

```sh
b3modtool txd export <disc>/DATA/FRONTEND.TXD work/fe
# edit work/fe/203_titlescreen_logo.png in any image editor: keep its size (512 x 256) and its number
mkdir -p hello-texture/data/DATA
b3modtool txd import <disc>/DATA/FRONTEND.TXD work/fe hello-texture/data/DATA/FRONTEND.TXD
b3modtool verify hello-texture --game <disc>
```

Copy the `hello-texture` folder into the mods folder (launcher: Mods > Open mods folder), press Look again, turn it on
and start the game: the title screen shows your logo. The game's log has
`mods: DATA/FRONTEND.TXD from hello-texture`. The tutorial explains each step and what the tools print.

## Sharing it

Do not share the `data/DATA/FRONTEND.TXD` you made: it is the game's dictionary with one picture changed (`b3modtool
pack --game` refuses it). Share a patch made against your disc instead:

```sh
mkdir -p share/hello-texture/patches/DATA
cp hello-texture/mod.ini hello-texture/README.md share/hello-texture/
b3modtool diff <disc>/DATA/FRONTEND.TXD hello-texture/data/DATA/FRONTEND.TXD share/hello-texture/patches/DATA/FRONTEND.TXD.b3patch
b3modtool pack share/hello-texture hello-texture.b3mod --game <disc>
```

A player installs `hello-texture.b3mod` (the launcher's Mods page: Add mod...) and turns it on; nothing else. When
the game starts it applies `patches/DATA/FRONTEND.TXD.b3patch` to the player's own `DATA/FRONTEND.TXD` (after checking
that it is the file the patch was made from: another version of the game turns the mod off for that run, with a
message) and keeps the result in the mods folder's `.cache`. The log has
`mods: DATA/FRONTEND.TXD from hello-texture (8517568 bytes, its patch applied to your disc's file)`. Tutorial 01,
step 9, has the details.
