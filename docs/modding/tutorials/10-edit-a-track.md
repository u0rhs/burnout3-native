# Tutorial 10: edit a track and put it in the game

In this tutorial you take one section of a route, repaint its road, raise a piece of scenery, recolour one of the
route's textures, and bring it back into the game as a mod that any player can install on their own disc. It follows
on from [tutorial 07](07-export-a-car.md) step 6 (export a route section) and works like
[tutorial 08](08-edit-a-car.md) does for cars. It takes about half an hour.

What you will do:

1. pick a route and a section;
2. export the section;
3. check that an unedited import gives the game's files back;
4. change it (in Blender, or with a script);
5. recolour a texture;
6. import into a mod folder;
7. check the mod and see it in the game;
8. learn what can and cannot change.

You need `b3modtool` ([tutorial 01](01-replace-a-texture.md#1-get-b3modtool)) and, for step 4, Blender 4.x (or any
glTF editor, or a script). In the examples `<disc>` is your disc: its folder or the disc image. The track import is
new: the terminal steps here were run as written; the Blender steps were run in Blender 4.2.23 LTS by a script
(`tools/modding/blender_check.sh`: Blender's own glTF importer and exporter with the options below, an unedited
round trip and an edit like step 4's), not yet by hand in its windows (feedback welcome).

**A track mod is visual.** The import changes what the game *draws*. The road the cars drive on (the collision), the
racing lines, traffic and AI paths stay the disc's: a road you raise is drawn higher while the cars keep driving on the
old one (step 8).

## 1. Pick a route and a section

```
b3modtool track list <disc>
```

```
TRACKS/AS/C1_V1      49 sections
...
TRACKS/US/C2_V2      37 sections
...
37 routes
```

A route is a course in one direction (`US/C2_V1`, `US/C2_V2`, ...); a **section** is one of the pieces the game streams
in as you drive (docs/track-format.md §2). A whole route is a big model (a few hundred thousand vertices); one section
is light, and Blender handles it easily. To find the section you want, export the whole route once and look at it
(`b3modtool preview`, tutorial 07), or try a few sections: each export's report gives its bounds.

## 2. Export the section

```
b3modtool track export <disc> US/C2_V2 c2 --section 10
```

```
TRACKS/US/C2_V2    164 files, 10 meshes, 59 nodes, 59 primitives, 6413 vertices, 3723 triangles, 156 textures; bounds (-2667.640 102.064 1345.158) .. (-2528.291 230.814 1623.705); 3.4 s; glTF check ok
```

`c2` now holds `US_C2_V2_s010.gltf` and `.bin` (section 10's high-detail meshes `section_010`, its far view
`section_010_lod`, and the objects placed in it), one PNG per texture of the route (`<NNN>_<name>.png`), the sidecar
`US_C2_V2_s010.b3track.json` and a report. **Keep the sidecar in this folder**: it tells the import which files of your
disc the export came from and where the textures are. These files are the game's data: never share them (the mod you
make in step 6 holds only your changes).

## 3. A first import, unedited

```
b3modtool track check <disc> c2
```

```
track check: TRACKS/US/C2_V2: 59 meshes in the model (59 unchanged, 0 edited in place, 0 with new triangles; 0 not in the model: kept), 0 packets moved, 0 blocks grown, 0 boxes grown, 0 materials and 0 textures swapped, 0 texture pictures changed; STATIC.DAT 5210112 -> 5210112, STREAMED.DAT 8650752 -> 8650752 bytes; ok, nothing written
```

Nothing changed, so nothing would be written: the import writes the game's own mesh packets back, and an unedited
export gives the route's `STATIC.DAT` and `STREAMED.DAT` byte for byte (checked for all 37 routes on the disc).

## 4. Change it

**In Blender:** **File > Import > glTF 2.0**, pick `c2/US_C2_V2_s010.gltf`. The objects:

- `section_010`: the section's road and scenery, one material slot per game material (`m54_road...`); this is what you
  see close up;
- `section_010_lod`: the same section for the far view (edit it too if your change should show from far away);
- `section_010_instance5_0`, `section_010_prop3_2` ...: objects the route places (trees, signs, lamps). Their meshes are
  shared by every copy along the route.

Things to try:

- **Repaint the road**: select `section_010`, switch to **Vertex Paint** and paint on the colour attribute (the game's
  vertex colours: they darken or tint the textures). The colours you see in Blender are the game's as they look on
  screen; the game stores a quarter of them (so the brightest 1.0 is the most a channel can show).
- **Raise or reshape**: in Edit Mode, move vertices (a bump in the road, a taller wall), or add and delete faces.
  Moving the whole `section_010` object in Object Mode works too: the import applies the move to the section's
  vertices (its placed objects stay where the game places them).
- **Edit a placed object's model**: in Edit Mode on one `instance` / `prop` object; every copy changes.

Rules:

- **Keep the object names and the material slots.** `section_010` and the materials `m<number>_...` are how the import
  knows which part of the files a mesh is. Blender's `.001` suffixes are fine.
- **Use the materials the section has.** A section has one mesh per material; faces moved to another of the section's
  materials go into that mesh. A new material (or one the section does not have) is refused.
- **Do not move, copy or delete the placed objects** (`instance` / `prop`): where they stand is part of the route's
  layout, which the import keeps (it refuses a model where one moved). Their shape you can edit.
- **No new objects**: join new geometry into `section_010` (**Ctrl J**).

Then **File > Export > glTF 2.0**: glTF Separate (`.gltf + .bin`) or Binary (`.glb`); **Include > Custom
Properties** on; **Data > Mesh > Attributes** on (they carry the stored colours and normals, `_B3_COLOR_R` .. `_A` and
`_B3_NRM_X` .. `_W`, one number per vertex each); **Data > Mesh > Vertex Colors** (or **Use Vertex Color**) on /
**Active**; **Data > Material > Materials**: Export (the default: the import knows a section's meshes by their
materials); Draco compression off. Save it into the `c2` folder (beside the sidecar), e.g. as
`c2/US_C2_V2_s010_edit.glb`; do not save over the export. (A `.gltf + .bin` saved there writes the route's PNGs again
beside it: the same files while you have not edited them.)

Blender joins the meshes of an object that has several of one material (some backdrops, signs and water) into one;
the import cuts them apart again by their triangles, and says so in a note. Such an object you can repaint, or move as
a whole in Object Mode; moving its vertices or adding faces is refused (its parts can no longer be told apart): leave
it as it is, or delete the object before you export (it then stays as the file has it).

To see what Blender itself changes, export once before you edit anything: `b3modtool track check <disc>
c2/US_C2_V2_s010_edit.glb` then says every mesh is unchanged and nothing would be written.

**Or with a script** (no Blender): the export is plain glTF. This takes the green out of every vertex colour of the
section's high-detail mesh (the port's own check, `tools/modding/track_edit_check.sh`, does the same to every section):

```python
import json
g = json.load(open('c2/US_C2_V2_s010.gltf'))
b = bytearray(open('c2/US_C2_V2_s010.bin', 'rb').read())
for m in g['meshes']:
    if m['name'] != 'section_010':
        continue
    for p in m['primitives']:
        a = g['accessors'][p['attributes']['COLOR_0']]       # normalized unsigned bytes, RGBA
        at = g['bufferViews'][a['bufferView']].get('byteOffset', 0) + a.get('byteOffset', 0)
        for i in range(a['count']):
            b[at + 4 * i + 1] = 0                             # green
open('c2/US_C2_V2_s010.bin', 'wb').write(b)
```

## 5. Recolour a texture

Each `<NNN>_<name>.png` in `c2` is one texture of the route (the road surface, a wall, a sign ...; `NNN` is its place in
the route's texture table: `000_Chgo_Pavement.png` is US/C2's pavement). Open one in your image editor, change its colours (a hue shift, levels, swapping channels)
or paint on it, and save it over the same file as PNG **at the same size**. The import writes it back into the
route's `STATIC.DAT` as `txd import` does (tutorial 01); the textures you did not touch stay as they are. A texture is
used wherever the route uses it, not only in your section.

To make a material use another of the route's textures, pick that PNG as the material's **Base Color** image in Blender
(only an existing PNG of the route: new textures cannot be added).

## 6. Import into a mod folder

```
b3modtool track import <disc> c2/US_C2_V2_s010_edit.glb green-road
```

(or `c2` for the export's own `.gltf`, after a script edit). With the script's colours and the pavement's red and blue
swapped:

```
  note: texture 0: Chgo_Pavement: mip levels rebuilt from level 0
  note: green-road/mod.ini written (id green-road): change the name, author and description before you share the mod
track import: TRACKS/US/C2_V2: 59 meshes in the model (27 unchanged, 32 edited in place, 0 with new triangles; 0 not in the model: kept), 0 packets moved, 0 blocks grown, 0 boxes grown, 0 materials and 0 textures swapped, 1 texture pictures changed; STATIC.DAT 5210112 -> 5210112, STREAMED.DAT 8650752 -> 8650752 bytes -> green-road/patches/TRACKS/US/C2_V2/STATIC.DAT.b3patch, green-road/patches/TRACKS/US/C2_V2/STREAMED.DAT.b3patch
```

What the counts mean: "edited in place" meshes kept their triangles and got new positions, colours or UVs; "with new
triangles" are meshes whose faces you changed (their strips are rebuilt: they may grow); "unchanged" ones are written
back as they were (after Blender, which reorders vertices, the import still sees that a mesh's triangles are the same).
A texture you changed makes the `STATIC.DAT.b3patch`.

The folder `green-road` is a mod (the two patches 11 KB and 15 KB):

```
green-road/
    mod.ini
    patches/
        TRACKS/
            US/
                C2_V2/
                    STATIC.DAT.b3patch
                    STREAMED.DAT.b3patch
```

If something cannot be represented the import says what and writes nothing, for example:

```
b3modtool: track import: section_010 primitive 3: section 10 has no mesh of material 77 (a unit's meshes are one per material, named by the render tables: the import cannot add one; use a material the section has) (nothing written)
```

**Limits.** The game loads each section into a fixed slot (160 KB for its high-detail block, 96 KB for its far view),
and many are nearly full: a lot of new triangles in one section may not fit, and the import then says by how much.
Remove detail elsewhere in that section, or spread the change over sections.

## 7. Check it and see it in the game

```
b3modtool verify green-road --game <disc>
```

```
verify: OK green-road: green-road 1.0, 3 files (0 disc paths, 2 patches), 0 errors, 0 warnings
```

Then, as in tutorial 01 step 7: put the folder into the mods folder (the launcher's **Mods** page, **Open mods
folder**), press **Look again** and **Turn on**, start the game and race on that route. The log says where the route's
files came from:

```
mods: TRACKS/US/C2_V2/STREAMED.DAT from green-road (8650752 bytes, its patch applied to your disc's file)
```

(and the same line for `STATIC.DAT`).

From a terminal: `native_play --game <disc> --mods green-road --mods-dir <the folder that holds green-road>`.

## 8. What changes in the game, and what does not

- **The picture only.** The collision (the surface the cars drive on and crash into), the start line, the racing lines
  and the traffic and AI paths (`GAMEDATA.BGD`) stay the disc's. A raised road is drawn above where the cars drive; a
  lowered one hides them. Keep geometry changes to what the cars do not touch (scenery, walls behind the barriers,
  decoration), or small.
- **The far view** (`_lod`) and the sections around it are separate meshes: a change shows from far away only if you
  make it there too.
- **The placed objects** stand where they stood; their models can change.
- **The sky** (`ENVIRO.DAT`'s textures in the export) is not imported yet.
- **Sharing.** Share the mod folder (or `b3modtool pack green-road green-road.b3mod`); never the export folder.

## What next

- [b3modtool.md](../b3modtool.md) "track import": every rule of the import, the census numbers.
- [file-formats/track.md](../file-formats/track.md): the route's files, how the mesh packets are written, the limits.
