# Runs inside Blender, headless, for tools/modding/blender_check.sh (which says how to run it):
#
#   blender -b --factory-startup --python-exit-code 1 --python tools/blender/blender_track_check.py -- \
#       --model <route export .gltf> --out <dir> [--lift <metres>]
#
# A route through Blender's own glTF importer and exporter, as tutorial 10 does it by hand (no add-on):
# 1. File > Import > glTF 2.0 of the export (the importer's defaults: one Blender vertex per glTF vertex, NORMAL as
#    custom normals, the "_" attributes as float mesh attributes);
# 2. File > Export > glTF 2.0, unedited, as <out>/track_unedited.glb (Include > Custom Properties, Data > Mesh >
#    Attributes, Use Vertex Color: Active; materials exported, no Draco);
# 3. an edit as tutorial 10 makes one: the green taken out of the colour attribute of every high-detail section (as
#    Vertex Paint would), and the first section moved --lift metres up in object mode; exported the same way as
#    <out>/track_edit.glb.
# Prints "blender_check(track): ..." lines; the last is "blender_check(track): PASS" or "... FAIL <why>" (exit 1).
import os
import re
import sys
import traceback

import bpy


def args():
    a = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    r = {}
    for i in range(0, len(a) - 1, 2):
        r[a[i].lstrip("-")] = a[i + 1]
    return r


A = args()
OUT = os.path.abspath(A["out"])
fails = []
SECTION = re.compile(r"^section_\d+$")


def say(msg):
    print("blender_check(track): " + msg)
    sys.stdout.flush()


def check(cond, what):
    if not cond:
        fails.append(what)
        say("FAIL " + what)
    return cond


def export(path):
    kw = dict(filepath=path, export_format="GLB", use_selection=False, export_extras=True, export_yup=True,
              export_texcoords=True, export_normals=True, export_attributes=True, export_tangents=False,
              export_materials="EXPORT", export_vertex_color="ACTIVE", export_draco_mesh_compression_enable=False,
              export_animations=False, export_skins=False, export_morph=False, export_apply=False,
              export_cameras=False, export_lights=False, will_save_settings=False, check_existing=False)
    have = {p.identifier for p in bpy.ops.export_scene.gltf.get_rna_type().properties}
    r = bpy.ops.export_scene.gltf(**{k: v for k, v in kw.items() if k in have or k == "filepath"})
    return check("FINISHED" in r and os.path.isfile(path), "export %s: %s" % (path, r))


def base(name):
    return name.split(".")[0]


def main():
    for ob in list(bpy.data.objects):
        bpy.data.objects.remove(ob)
    r = bpy.ops.import_scene.gltf(filepath=os.path.abspath(A["model"]), merge_vertices=False, import_shading="NORMALS")
    if not check("FINISHED" in r, "import %s: %s" % (A["model"], r)):
        return
    meshes = [ob for ob in bpy.data.objects if ob.type == "MESH"]
    sections = sorted((ob for ob in meshes if SECTION.match(base(ob.name))), key=lambda ob: ob.name)
    attrs = sorted({a.name for ob in meshes for a in ob.data.attributes if a.name.startswith("_B3_")})
    forms = sorted({a.data_type for ob in meshes for a in ob.data.attributes if a.name.startswith("_B3_")})
    say("imported %s: %d objects, %d mesh objects (%d high-detail sections); attributes %s (%s)"
        % (os.path.basename(A["model"]), len(bpy.data.objects), len(meshes), len(sections), " ".join(attrs),
           " ".join(forms)))
    check(sections, "no section_NNN objects")
    check(forms == ["FLOAT"], "the _B3_ attributes are not all FLOAT: %s" % forms)
    os.makedirs(OUT, exist_ok=True)
    if not export(os.path.join(OUT, "track_unedited.glb")):
        return
    say("exported unedited: track_unedited.glb")

    # the edit: green out of every high-detail section's colour attribute, the first section lifted in object mode
    corners = 0
    for ob in sections:
        ca = ob.data.color_attributes.get("Color") or (ob.data.color_attributes[0] if ob.data.color_attributes else None)
        if ca is None:
            continue
        prop = "color_srgb" if ca.data_type == "BYTE_COLOR" else "color"
        n = len(ca.data)
        v = [0.0] * (4 * n)
        ca.data.foreach_get(prop, v)
        for i in range(n):
            v[4 * i + 1] = 0.0
        ca.data.foreach_set(prop, v)
        ob.data.update()
        corners += n
    check(corners > 0, "no colour attribute on the sections")
    lift = float(A.get("lift", "0.5"))
    first = sections[0] if sections else None
    if first is not None:
        first.location.z += lift   # Blender's up is +Z (the export's +Y)
    bpy.context.view_layer.update()
    say("edited: the green out of %d sections' colours (%d corners); %s lifted %.2f m"
        % (len(sections), corners, first.name if first else "-", lift))
    if export(os.path.join(OUT, "track_edit.glb")):
        say("exported the edit: track_edit.glb (lifted %s)" % (first.name if first else "-"))


try:
    main()
except Exception:
    traceback.print_exc()
    fails.append("exception (above)")
if fails:
    print("blender_check(track): FAIL " + "; ".join(fails))
    sys.stdout.flush()
    sys.exit(1)
print("blender_check(track): PASS")
