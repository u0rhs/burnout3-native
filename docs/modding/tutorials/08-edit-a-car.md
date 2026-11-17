# Tutorial 08: edit a car in Blender and put it in the game

In this tutorial you take one of the game's cars, make its body bigger in Blender, give it a new colour in an image
editor, and bring it back into the game as a mod that any player can install on their own disc. It follows on from
[tutorial 07](07-export-a-car.md) (export a car and open it in Blender). It takes about half an hour.

What you will do:

1. export the car;
2. check that an unedited import gives the game's file back;
3. change the shape in Blender;
4. recolour a paint;
5. export from Blender;
6. import into a mod folder;
7. check the mod and see it in the game;
8. learn what can and cannot change.

You need `b3modtool` ([tutorial 01](01-replace-a-texture.md#1-get-b3modtool)), Blender 4.x and any image editor. In
the examples `<disc>` is your disc: its folder or the disc image.

## 1. Export the car

As in tutorial 07:

```
b3modtool car export <disc> "Compact Type 1" car1
```

`car1` now holds `COMP_CAR1.gltf`, `COMP_CAR1.bin`, the paint pictures `COMP_CAR1_paint0.png` .. `_paint7.png` and the
sidecar `COMP_CAR1.b3car.json`. **Keep the sidecar in this folder**: it tells the import which file of your disc the
car came from and where the pictures are. These files are the game's data: never share them (the mod you make in step 6
holds only your changes).

## 2. A first import, unedited

Before you change anything, import the export as it is:

```
b3modtool car import <disc> car1 car1-unchanged.BGV
```

```
car import: PVEH/COMP/CAR1.BGV: 114 meshes (114 unchanged, 0 edited in place, 0 with new triangles, 0 not in the model: kept), 0 packets moved, 0 paints changed; 436224 -> 436224 bytes -> car1-unchanged.BGV
```

The new file is your disc's `PVEH/COMP/CAR1.BGV` byte for byte: the import writes the game's own mesh packets, so
anything you do not change stays exactly as it was. (This was checked for all 67 player cars on the disc.) Delete
`car1-unchanged.BGV`.

## 3. Change the shape in Blender

1. **File > Import > glTF 2.0**, pick `car1/COMP_CAR1.gltf` (tutorial 07 step 5 explains what you see).
2. Hide everything but **LOD3** (the detailed car you see close up). Its body is the objects under **LOD3_L0**:
   `LOD3_L0_e00`, `LOD3_L0_e01` ... (one per material of the body); the panels that bend and come off in a crash
   (bonnet, doors, boot ...) are the objects under **LOD3_A0** .. **LOD3_A5**. Scale both, or the panels stay their
   old size on a bigger body.
3. Put the 3D cursor at the car's origin: **Shift C**. In Object Mode, select the `LOD3_L0_e..` objects and the
   `LOD3_A0_e..` .. `LOD3_A5_e..` objects (in the Outliner: click the first, Ctrl-click the others), set the **Pivot
   Point** (the header of the 3D view) to **3D Cursor**, then press **S 1.3 Enter**: the body and its panels are 30 %
   bigger, around the car's origin. The import applies each object's new scale and place to its vertices (the panels
   still turn around their own hinges in the game) and keeps the game's own triangle strips, so the file keeps its
   size. Leave the wheels (`LOD3_B0_w..`) alone: they are placed by the game at the wheel frames.

   Editing in Edit Mode works too (the same pivot); Blender then saves the mesh in its own vertex order, so the import
   builds new strips for the meshes you edited, and the file may grow by a few per cent (step 6).

Some rules for what you change:

- **Keep the object names.** `LOD3_L0_e00` is how the import knows which part of the file a mesh is. Blender's
  `.001` suffixes are fine.
- **Edit the meshes that are there.** You can move, scale, rotate and remodel vertices, add and delete faces, and move
  UVs. You cannot add new objects: join a new part into one of the existing objects (**Ctrl J**) instead.
- **The panels and wheels** (`LOD3_A0` .. `LOD3_A5`, `LOD3_B0_w0` ..) turn around their own pivots in the game; if you
  move one of those objects, the import moves its vertices instead, around the old pivot. The four wheels share one
  mesh: edit it once.
- **Not imported:** the light empties (`lights`), materials (each mesh keeps the game's material) and the other
  paints' material variants.

## 4. Recolour a paint

Each `COMP_CAR1_paint<k>.png` is the car's texture in one of its paint colours (the colours you pick in the car select
menu, in order). Open `car1/COMP_CAR1_paint0.png` in your image editor and change its colours, for example a hue shift
(GIMP: **Colors > Hue-Saturation**), then save it over the same file as PNG, **at the same size and with its alpha**
(the alpha is a mask the game uses, not transparency).

- A change of colours (hue, levels, swapping channels, any change that turns each colour into another one) changes
  only that paint, exactly.
- Painting on the picture (a stripe, a number) works too: the import then rebuilds the texture's 256 colours for all
  paints together, so each paint shows what its PNG shows, as closely as 256 colours allow.
- Pictures you did not change stay as they are.

## 5. Export from Blender

**File > Export > glTF 2.0 (.glb/.gltf)**, and in the export window:

- **Format**: glTF Binary (`.glb`) or glTF Separate (`.gltf + .bin + textures`), as you like;
- **Include > Data > Custom Properties**: on (carries the game's keys in `extras`);
- **Data > Mesh > Attributes**: on (carries `_B3_NRM_X` .. `_Z` and `_B3_INFL_0` / `_1`, the stored normals and the
  damage influences);
- the rest as it is.

Save it **into the `car1` folder** (beside the sidecar), e.g. as `car1/COMP_CAR1_big.glb`. Do not save over
`COMP_CAR1.gltf` (keep the export as it was).

If an option is missing in your Blender version, export anyway: without the attributes the import makes normals from
`NORMAL` and gives new vertices the damage influences of the nearest original vertex.

## 6. Import into a mod folder

```
b3modtool car import <disc> car1/COMP_CAR1_big.glb big-compact
```

```
  note: 25 object(s) moved / turned / scaled (LOD3_L0_e00 .. LOD3_A5_e02): applied to their vertices (the parts' frames stay the file's)
  note: compact1: a recolour (each palette entry one colour): only the edited paints' CLUTs change
  note: big-compact/mod.ini written (id big-compact): change the name, author and description before you share the mod
car import: PVEH/COMP/CAR1.BGV: 114 meshes (89 unchanged, 25 edited in place, 0 with new triangles, 0 not in the model: kept), 0 packets moved, 1 paints changed; 436224 -> 436224 bytes -> big-compact/patches/PVEH/COMP/CAR1.BGV.b3patch
```

What the counts mean: "edited in place" meshes kept their triangles and got new positions; "with new triangles" are
meshes whose faces you changed (their strips are rebuilt; the file may grow); "unchanged" ones are written back as
they were (after a round trip through Blender, which reorders vertices, the import still sees that a mesh's triangles
are the same).

The folder `big-compact` is a mod:

```
big-compact/
    mod.ini
    patches/
        PVEH/
            COMP/
                CAR1.BGV.b3patch
```

The patch holds only your changes; the game applies it to each player's own `CAR1.BGV` when it starts. Open
`mod.ini` and give the mod your name and description ([getting-started.md](../getting-started.md)).

If something is wrong the import says what and writes nothing, for example:

```
b3modtool: car import: "Cube" is not one of the car's meshes (names like LOD3_L0_e00): the import changes the meshes the file has and cannot add objects; join new parts into one of them (nothing written)
```

A car file has a size limit: a player car is loaded into a 466,944-byte block, and some cars already fill it. If your
new triangles do not fit, the import says by how much; remove detail elsewhere (the lower LODs `LOD0` .. `LOD2` are
only seen from far away).

## 7. Check it and see it in the game

```
b3modtool verify big-compact --game <disc>
```

```
verify: OK big-compact: big-compact 1.0, 2 files (0 disc paths, 1 patches), 0 errors, 0 warnings
```

`verify: OK` means the game will load it (the patch is 84 KB: only the changed bytes). Then, as in tutorial 01 step 7: put the folder into the mods folder (the
launcher's **Mods** page, **Open mods folder**), press **Look again** and **Turn on**, start the game and pick
**Compact Type 1** in paint 0 for a race. The log says where the car came from:

```
mods: PVEH/COMP/CAR1.BGV from big-compact (436224 bytes, its patch applied to your disc's file)
```

From a terminal: `native_play --game <disc> --mods big-compact --mods-dir <the folder that holds big-compact>`.

The port's check `tools/modding/car_mod_check.sh` does the same for every car without a window (every car's body
and panels 1.3x, the paints' red and blue swapped) and compares race screenshots with and without the mod.

## 8. What changes in the game, and what does not

- **The picture only.** The car's collision shape and handling are in other parts of the file, which the import keeps
  as they are: a bigger body still collides like the old one, and a smaller one floats a little.
- **Damage** bends the new shape by the influences of its vertices (`_B3_INFL_0` / `_1`): vertices you add take the nearest
  original vertex's.
- **Levels of detail.** From far away the game draws `LOD0` .. `LOD2`; edit them too if your change should show at a
  distance.
- **Sharing.** Share the mod folder (or `b3modtool pack big-compact big-compact.b3mod`); never the export folder.

## What next

- [b3modtool.md](../b3modtool.md) "car import": every rule of the import, the census numbers.
- [file-formats/car.md](../file-formats/car.md): the car file, and how the mesh packets are written.
- [Tutorial 09](09-car-mods-in-blender.md): the same from Blender's menus with the add-on B3 Car Tools.
