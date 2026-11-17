# Tutorial 07: export a car and open it in Blender

In this tutorial you export one of the game's cars from your disc as a glTF 2.0 model, look at it without any other
tool, and open it in Blender: its levels of detail, the panels that come off in a crash, the wheels, the paints and the
light points. It takes about ten minutes.

This is the **export** half of the model tools (mod kit part 3c); [tutorial 08](08-edit-a-car.md) is the other half:
a car you change in Blender goes back into the game with `b3modtool car import`. Exporting alone is also useful to study
a car, measure it, make renders, or start a new model with the game's proportions; the files keep everything the import
needs (see [b3modtool.md](../b3modtool.md) "The export's files").

What you will do:

1. get `b3modtool`;
2. find the car by its name;
3. export it;
4. look at it with `b3modtool preview`;
5. open it in Blender and find your way around it;
6. (optional) export a route section the same way.

The exported files are made from your disc: they are the game's data. Keep them for your own work; never share them.

## 1. Get b3modtool

As in [tutorial 01](01-replace-a-texture.md#1-get-b3modtool): `b3modtool.exe` (Windows) or `b3modtool` (Linux) beside
the launcher in a release, or `make modtool` (`build/b3modtool`) from the source. In the examples `<disc>` is your disc:
its folder or the disc image.

## 2. Find the car

```
b3modtool car list <disc>
```

prints the 67 player cars in the game's order with their names, then the traffic vehicles:

```
  0  PVEH/COMP/CAR1.BGV   Compact      Compact Type 1
  1  PVEH/COMP/CAR2.BGV   Compact      Compact Type 2
  ...
 53  PVEH/HSPC/CAR23.BGV  Special      Classic Hotrod
 54  PVEH/HEVY/CAR36.BGV  Special      Fire Truck
  ...
67 player cars, 40 traffic vehicles
```

Any of the three columns names a car for the next step: its number (`53`), its name (`"Classic Hotrod"`, in quotes
because of the space) or its path (`PVEH/HSPC/CAR23.BGV`).

## 3. Export it

```
b3modtool car export <disc> "Compact Type 1" car1
```

```
PVEH/COMP/CAR1.BGV   Compact Type 1         11 files, 114 meshes, 310 nodes, 11824 vertices, 7320 triangles, 8 paints; bounds (-1.016 -0.314 -2.087) .. (1.016 1.122 2.064); glTF check ok
  note: ...
car export: 1 of 1 cars written into car1, 1 glTF checked valid (the game's data: for your own editing, never for sharing)
```

The folder `car1` now holds:

- `COMP_CAR1.gltf` and `COMP_CAR1.bin`: the model. Keep them together (the `.gltf` names the `.bin` and the pictures);
- `COMP_CAR1_paint0.png` .. `COMP_CAR1_paint7.png`: the car's texture, once per paint colour;
- `COMP_CAR1.b3car.json`: the sidecar for the import (offsets, material words, hashes; you do not need it in Blender,
  but keep it beside the model for tutorial 08).

The bounds are in metres: this car is 2.03 m wide, 1.44 m high and 4.15 m long, its front towards +Z.

Options: `--lod 3` exports only the highest level of detail (`0` is the lowest); `--paint 2` makes paint 2 the default
material; `--format obj` writes OBJ / MTL files instead, for tools without glTF (one file per LOD, no paints to switch).

## 4. Look at it

```
b3modtool preview car1/COMP_CAR1.gltf car1.png --skip _B1_,_B2_,extra_lists
```

draws the model into `car1.png` (no window opens). The `--skip` words leave out the blurred wheels and the two extra
lists (step 5 says what they are); `--yaw 200` looks from behind, `--pitch 60` from above. If the picture shows the car,
the export is fine.

## 5. Open it in Blender

These steps are for Blender 4.x; its glTF importer is built in.

1. **File > Import > glTF 2.0 (.glb/.gltf)**, pick `COMP_CAR1.gltf`, and press **Import glTF 2.0**. The defaults are
   right: Blender turns the file's Y-up axes into its own Z-up, and one glTF unit is one metre, so the car stands on
   the ground plane at its real size. (An export made by an older `b3modtool`, with the attributes `_B3_NRM` and
   `_B3_INFL`, can stop Blender's import with a Python error, `ValueError: all the input array dimensions ... must
   match exactly`: export the car again with this one.)
2. In the **Outliner** the car is one tree under the empty **Compact Type 1**:
   - **LOD0 .. LOD3**: the four levels of detail; LOD3 is the one you see close up, LOD0 the distant one. They sit on
     top of each other: hide all but one (the eye icon) to see a single car.
   - inside each LOD, **LOD3_L0** is the body; **LOD3_A0 .. LOD3_A5** the panels that bend and come off (bonnet, doors,
     boot ...), each at its own origin; **LOD3_B0_w0 .. w3** the wheel, once per wheel (the four objects share one mesh:
     editing one edits all); **LOD3_B1_*** and **LOD3_B2_*** are the motion-blurred wheels the game swaps in at speed:
     hide them;
   - under each part, one object per mesh of the game's file (**LOD3_L0_e00**, **_e01** ...): one material each;
   - **extra_lists**: two small meshes the file holds outside the LODs (what the game uses them for is not known yet);
   - **lights**: empties at the light points: `light_head_*`, `light_tail_*`, `light_brake_*`, `light_reverse_*`,
     `light_indicator_a_*` / `_b_*` (the two sides), `light_exhaust_*` (where the boost flame comes out), and other
     points as `light_g<k>_*`.
3. **Materials.** Switch the viewport to **Material Preview** (Z, then 2). Materials are named `paint0_body`,
   `paint0_light` and `paint0_glass_<opacity>`; glass is blended. The car's texture is one picture for the whole car.
4. **Paints.** The other paint colours come as material variants. In **Edit > Preferences > Add-ons**, open the glTF
   2.0 add-on's settings and turn on its material variants option; then the 3D view's sidebar (N) has a **glTF
   Variants** tab: pick "paint 0" .. "paint 7" and press **Display Variant**. (The option's place has moved between
   Blender versions; if you cannot find it, open `COMP_CAR1_paint3.png` in the image editor and assign it to the
   materials by hand.)
5. **What the game knows about each object.** Select an object and open **Object Properties > Custom Properties**:
   `b3` holds its part, wheel or entry; a mesh's own properties (Object Data Properties) hold its material and colour
   words. Blender also lists the game's per-vertex data under **Attributes** (`_B3_NRM_X` .. `_Z`, `_B3_INFL_0`,
   `_B3_INFL_1`, `_B3_W`, one number per vertex each); `_B3_INFL_0` / `_1` are what the game's damage uses: which zone
   of the car a vertex belongs to and how strongly it follows it.
6. **Damage.** There are no damaged shapes to see: the game bends the panels while you drive, from those influences.

To see the car as in the game: keep only **LOD3**, hide **LOD3_B1_*** / **LOD3_B2_***, **extra_lists** and
**lights**.

## 6. A route section (optional)

Routes export the same way, split into sections (the pieces the game streams in as you drive):

```
b3modtool track list <disc>
b3modtool track export <disc> US/C1_V1 c1 --section 10
b3modtool preview c1/US_C1_V1_s010.gltf section10.png --skip _lod
```

`--section 10` gives one section with its trees, signs and other placed models; without it you get the whole route
(about 300,000 vertices and 200 textures: Blender handles it, slowly). `section_010_lod` is the same piece of road as
seen from far away. The file `US_C1_V1_s010_report.txt` lists what is not exported (line meshes, the collision,
the sky dome). Route coordinates are the game's world coordinates in metres, kilometres from the origin: in Blender
press **View > Frame Selected** (numpad .) to find the section.

## What next

- [b3modtool.md](../b3modtool.md) "car list / export", "track list / export": every option and file.
- [file-formats/car.md](../file-formats/car.md): what a car file holds, part by part.
- [Tutorial 08](08-edit-a-car.md): edit the car in Blender and put it in the game.
