# Tutorial 09: car mods with the Blender add-on

In this tutorial you make the car mod of [tutorial 08](08-edit-a-car.md) without a terminal: the **B3 Car Tools**
add-on for Blender imports a car straight from your disc, checks your edits, and writes the mod. It takes about
fifteen minutes.

> **The add-on is new.** It is a thin front end on `b3modtool`, which does all the work on the game's files and is
> checked on every car of the disc. The add-on is checked in Blender 4.2.23 LTS by `tools/modding/blender_check.sh`
> (run headless, without a window): it installs and enables the add-on, imports three cars with **File > Import**,
> runs **Check**, exports each unedited car to a car file (the disc's bytes, byte for byte), scales Compact Type 1's
> body and panels 1.3x in object mode, exports that as a mod folder, and races with the mod. The clicks in Blender's
> windows have had little use yet: if a step does not work as written here, or a message does not help you, please
> tell us (an issue on the port's repository, with your Blender version and what the add-on said). Feedback and fixes
> are welcome.

What you will do:

1. install the add-on;
2. tell it where your disc and `b3modtool` are;
3. import a car;
4. make its body bigger;
5. check it;
6. recolour a paint;
7. export the mod;
8. see it in the game.

You need Blender 4.2 or newer (checked with 4.2.23 LTS), `b3modtool` ([tutorial 01](01-replace-a-texture.md#1-get-b3modtool)) and the add-on's
zip, `b3_car_tools.zip`: it is in the port's release beside `b3modtool`, or `make blender-addon` builds it from the
source (`build/b3_car_tools.zip`). In the examples `<disc>` is your disc: its folder or the disc image.

## 1. Install the add-on

1. In Blender, **Edit > Preferences > Get Extensions**, the drop-down arrow at the top right, **Install from Disk...**,
   and pick `b3_car_tools.zip`. (It also installs as a classic add-on: **Add-ons**, the same arrow, **Install from
   Disk...**.)
2. In **Add-ons**, find **B3 Car Tools** and tick it if it is not on.

## 2. Tell it where things are

Open the add-on's preferences (the arrow beside its name in **Add-ons**):

- **b3modtool**: the folder of the port's release (where the launcher and `b3modtool` are), or the `b3modtool`
  program itself. Left empty, the add-on looks for `b3modtool` on your `PATH`. The line under the settings says which
  `b3modtool` it found.
- **Disc**: your disc, its folder or the disc image. Nothing is ever written to it.
- **Work folder**: where the add-on puts the cars it exports from your disc (one folder per car; `b3-car-work` in
  your home folder by default). These files are the game's data: they are for your own editing, never for sharing.
- **Apply modifiers**: on, a Mirror or Subdivision modifier you add ends up in the game as you see it.

## 3. Import a car

**File > Import > Burnout 3 Car (b3modtool)**. In the window that opens, click **Car**, type a few letters of its
name (`comp`) and pick **Compact Type 1**, then **OK**.

The add-on runs `b3modtool car export` into the work folder (`b3-car-work/compact-type-1`: the model, the paint
pictures `COMP_CAR1_paint0.png` .. `_paint7.png` and the sidecar `COMP_CAR1.b3car.json`) and opens the model with
Blender's own glTF importer, with the settings the round trip needs. The car is in a new collection, **B3 Compact
Type 1**, laid out as [tutorial 07](07-export-a-car.md#5-open-it-in-blender) step 5 describes: the empty **Compact
Type 1**, its levels of detail **LOD0** .. **LOD3**, the body **LOD3_L0**, the panels **LOD3_A0** .. **LOD3_A5**, the
wheels, the lights.

The sidebar of the 3D view (**N**) has a **B3 Car** tab. With any object of the car selected it shows the car, the
file it came from, and the buttons this tutorial uses.

Import a car again and you get a second collection and a second export folder (`compact-type-1_2`): an import never
writes over an earlier export, whose paint pictures may hold your edits. The second car's objects get Blender's
`.001` names; that is fine.

## 4. Make the body bigger

As in tutorial 08 step 3: hide all but **LOD3**, press **Shift C** (the 3D cursor to the car's origin), select the
`LOD3_L0_e..` objects and the `LOD3_A0_e..` .. `LOD3_A5_e..` objects, set the **Pivot Point** to **3D Cursor**, and
press **S 1.3 Enter**. Leave the wheels alone.

The rules of tutorial 08 step 3 hold: keep the object names, edit the meshes that are there (join a new part into
one of them with **Ctrl J**; new objects are refused), and the lights and materials are not imported.

## 5. Check it

In the sidebar's **B3 Car** tab, press **Check**. The add-on saves the car with Blender's glTF exporter beside the
sidecar (`COMP_CAR1_blender.glb`) and runs `b3modtool car check`, which does everything the import does but writes
nothing. The status bar says what it found:

```
Check OK: 114 meshes: 89 unchanged, 25 edited in place, 0 with new triangles; 436224 bytes (same size)
```

"Edited in place" meshes keep the game's own triangle strips with your new positions, so the file keeps its size;
"with new triangles" are meshes whose faces you changed (their strips are rebuilt; the file may grow). Notes, if any,
are in the **Info** editor (and Blender's system console).

When something is wrong, the check says what and what to do. For example, with a cube added to the car's collection:

```
An object that is not part of the car: "Cube" is not one of the car's meshes (names like LOD3_L0_e00): ... -- the
import can only change the car's own objects: join the new part into one of them (select it, then the car's object,
Ctrl J) or move it out of the car's collection
```

and the cube is selected for you. The messages you may meet are listed under "When the add-on says no" below.

## 6. Recolour a paint

Press **Export Folder** in the sidebar: your file manager opens the car's export folder. Edit `COMP_CAR1_paint0.png`
as tutorial 08 step 4 says (same size, keep the alpha) and save it over the same file. In Blender, **Image > Reload**
in an image editor shows the change.

You can also paint in Blender (**Texture Paint**): the add-on saves paint pictures you painted on and did not save
before it checks or exports.

## 7. Export the mod

**File > Export > Burnout 3 Car Mod (b3modtool)**, or **Export to Mod...** in the sidebar. Choose what to make:

- **Mod folder** (the default): a folder with `mod.ini` and `patches/PVEH/COMP/CAR1.BGV.b3patch`, only your changes,
  ready to share (as tutorial 08 step 6 makes);
- **.b3mod file**: the same folder, beside it, packed into one file to share (`b3modtool pack`);
- **Car file**: the whole new `CAR1.BGV`, to try on your own PC; never share this one.

**Where** is filled in under your work folder (`b3-car-work/mods/compact-type-1-mod`); change it as you like, then
**OK**. The add-on exports the glTF again, runs `b3modtool car import` into the mod and `b3modtool verify --game
<disc>` on it, and reports:

```
Mod written: .../compact-type-1-mod (114 meshes: 89 unchanged, 25 edited in place, 0 with new triangles; 1 paint(s)
changed; 436224 bytes (same size)); verify: OK compact-type-1-mod: ...
```

**Mod Folder** in the sidebar opens it. Open its `mod.ini` and give the mod your name and a description
([getting-started.md](../getting-started.md)).

## 8. See it in the game

As in tutorial 08 step 7: put the mod folder into the mods folder (the launcher's **Mods** page, **Open mods
folder**), press **Look again** and **Turn on**, start the game and race **Compact Type 1** in paint 0.

## Your .blend

Save the `.blend` anywhere. The car's root empty keeps where its export folder is and a copy of the sidecar (its
custom property `b3_car`), so **Check** and **Export to Mod** work after you reopen it. Keep the export folder: if its
sidecar is gone, the add-on writes it back from the `.blend`, but the paint pictures are only there. The `.blend` and
the export folder hold the game's data: share the mod, never these.

## When the add-on says no

Nothing is written when `b3modtool` refuses; the message says why. The common ones:

| The add-on says | What to do |
|---|---|
| An object that is not part of the car | join the new part into one of the car's objects (**Ctrl J**), or move it out of the car's collection |
| Two objects for one part of the car | keep one object per part; the four wheels share one mesh: edit it once |
| UVs off the texture | keep that object's UVs on the texture (the UV editor's square) |
| The car is too big for the game | a player car must fit 466,944 bytes: remove detail, e.g. in **LOD0** .. **LOD2**, which are only seen from far away |
| A vertex far away from the car | a stray vertex more than 1 km out: find it in Edit Mode |
| A paint picture of another size | save the PNG at its exported size, with its alpha |
| This car came from another version of the file | the disc in the preferences is not the one the car was exported from: import the car again |
| The export's sidecar is missing | import the car again (the add-on writes the sidecar back from the `.blend` when it can) |
| b3modtool was not found | set **b3modtool** in the add-on's preferences |
| This b3modtool is too old for the add-on | use the `b3modtool` of the same release as the add-on |

## What the add-on does for you

Every step is a `b3modtool` command (tutorials 07 and 08 run them by hand):

| Button | Runs |
|---|---|
| Import | `b3modtool car export <disc> PVEH/COMP/CAR1.BGV <work>/compact-type-1`, then Blender's glTF import |
| Check | Blender's glTF export (GLB), then `b3modtool car check <disc> <glb> --sidecar <sidecar>` |
| Export to Mod | the same export, `b3modtool car import <disc> <glb> <mod> --sidecar <sidecar>`, `verify`, and `pack` for a .b3mod |

Its glTF export keeps what the import needs: the `_B3_*` attributes (the stored normals and damage influences, one
number per vertex each: `_B3_NRM_X` .. `_Z`, `_B3_INFL_0`, `_B3_INFL_1`, `_B3_W`) and the custom properties (the parts' names in `extras`), no Draco compression, no materials or images (the import keeps the
game's materials; the paints are read from the PNGs), only the car's collection. If you export by hand instead, use
the settings of tutorial 08 step 5.

## What next

- [b3modtool.md](../b3modtool.md) "car import" and "Blender": every rule of the import, and what Blender's glTF
  importer and exporter do to a car.
- [Tutorial 08](08-edit-a-car.md): the same mod with `b3modtool` by hand, and what changes in the game.
