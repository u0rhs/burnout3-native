# Runs inside Blender, headless, for tools/modding/blender_check.sh (which says how to run it):
#
#   blender -b --factory-startup --python-exit-code 1 --python tools/blender/blender_check.py -- \
#       --zip <b3_car_tools.zip> --modtool <b3modtool> --disc <disc folder> --out <dir> --cars "A|B|C" --mod-name <id>
#
# 1. installs the add-on zip (as a legacy add-on, into the BLENDER_USER_* folders the shell script points at) and
#    enables it; sets its preferences (b3modtool, the disc, a work folder under --out);
# 2. imports each car of --cars with the add-on's File > Import operator (b3.import_car), into one scene;
# 3. for each car: Check (b3.check_car), then an unedited export to a car file (b3.export_mod kind FILE): the bytes
#    must be the disc's file;
# 4. the first car's LOD 3 body and panels scaled 1.3x about the car's origin in object mode (tutorial 08), exported
#    as a mod folder (b3.export_mod kind FOLDER, --out/mods/<mod-name>): verify OK, the same size, edited in place.
# Prints "blender_check: ..." lines; the last is "blender_check(blender): PASS" or "... FAIL <why>" (exit status 1).
# Run in Blender 4.2.23 LTS (headless) by blender_check.sh.
import os
import sys
import traceback

import bpy
from mathutils import Matrix


def args():
    a = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    r = {}
    for i in range(0, len(a) - 1, 2):
        r[a[i].lstrip("-")] = a[i + 1]
    return r


A = args()
OUT = os.path.abspath(A["out"])
fails = []


def say(msg):
    print("blender_check: " + msg)
    sys.stdout.flush()


def check(cond, what):
    if not cond:
        fails.append(what)
        say("FAIL " + what)
    return cond


def disc_file(disc, key):
    """The disc's own file for a path like PVEH/COMP/CAR1.BGV (any spelling of its folders)"""
    at = disc
    for part in key.split("/"):
        names = {n.upper(): n for n in os.listdir(at)}
        if part.upper() not in names:
            return ""
        at = os.path.join(at, names[part.upper()])
    return at


def root_of(coll):
    for ob in coll.all_objects:
        if "b3_car" in ob:
            return ob
    return None


def activate(root):
    vl = bpy.context.view_layer
    for ob in vl.objects:
        ob.select_set(False)
    vl.objects.active = root
    root.select_set(True)


def main():
    # 1. the add-on
    r = bpy.ops.preferences.addon_install(filepath=A["zip"], overwrite=True)
    check("FINISHED" in r, "addon_install: %s" % r)
    bpy.ops.preferences.addon_enable(module="b3_car_tools")
    if "b3_car_tools" not in bpy.context.preferences.addons:
        # the install went somewhere this Blender does not look (a factory-startup's script paths?): say so, then
        # enable it from the zip's folder so the rest is still checked
        fails.append("addon_install + addon_enable did not enable the add-on")
        import addon_utils
        import zipfile
        src = os.path.join(OUT, "addon-src")
        zipfile.ZipFile(A["zip"]).extractall(src)
        sys.path.insert(0, src)
        addon_utils.enable("b3_car_tools", default_set=True)
    check("b3_car_tools" in bpy.context.preferences.addons, "the add-on is not enabled")
    p = bpy.context.preferences.addons["b3_car_tools"].preferences
    p.b3modtool = os.path.abspath(A["modtool"])
    p.disc = os.path.abspath(A["disc"])
    p.work_dir = os.path.join(OUT, "work")
    p.apply_modifiers = True
    say("add-on enabled: %s" % bpy.context.preferences.addons["b3_car_tools"].module)
    for n in ("import_car", "check_car", "export_mod", "open_folder"):
        check(hasattr(bpy.ops.b3, n), "no operator b3.%s" % n)

    # 2. three cars into one scene (the second and third get Blender's .001 names: the import must not mind)
    cars = [c for c in A["cars"].split("|") if c]
    roots = []
    for name in cars:
        before = set(bpy.data.collections)
        r = bpy.ops.b3.import_car(car=name)
        if not check("FINISHED" in r, "import %s: %s" % (name, r)):
            continue
        new = [c for c in bpy.data.collections if c not in before]
        root = root_of(new[0]) if new else None
        if not check(root is not None, "import %s: no root with the b3_car property" % name):
            continue
        props = root["b3_car"].to_dict()
        meshes = sum(1 for ob in new[0].all_objects if ob.type == "MESH")
        say("imported %s: %s, %d mesh objects, export %s" % (name, props["source_path"], meshes, props["export_dir"]))
        check(meshes > 0, "import %s: no meshes" % name)
        roots.append(root)

    # 3. unedited: Check, then the car file, byte for byte the disc's
    os.makedirs(os.path.join(OUT, "roundtrip"), exist_ok=True)
    for root in roots:
        props = root["b3_car"].to_dict()
        activate(root)
        r = bpy.ops.b3.check_car()
        check("FINISHED" in r, "check %s: %s" % (props["name"], r))
        out = os.path.join(OUT, "roundtrip", props["source_path"].replace("/", "_"))
        r = bpy.ops.b3.export_mod(kind="FILE", filepath=out)
        if not check("FINISHED" in r and os.path.isfile(out), "export %s to a car file: %s" % (props["name"], r)):
            continue
        orig = disc_file(A["disc"], props["source_path"])
        with open(orig, "rb") as f:
            a = f.read()
        with open(out, "rb") as f:
            b = f.read()
        first = next((i for i in range(min(len(a), len(b))) if a[i] != b[i]), min(len(a), len(b)))
        same = a == b
        check(same, "%s: the unedited round trip through Blender is not the disc's file (%d -> %d bytes, first "
                    "difference at 0x%X)" % (props["name"], len(a), len(b), first))
        if same:
            say("%s: unedited round trip through Blender: the disc's %s byte for byte (%d bytes)"
                % (props["name"], props["source_path"], len(a)))

    # 4. the first car's body and panels 1.3x in object mode, as a mod folder
    if roots:
        root = roots[0]
        props = root["b3_car"].to_dict()
        coll = root.users_collection[0]
        S = Matrix.Scale(1.3, 4)
        n = 0
        for ob in coll.all_objects:
            base = ob.name.split(".")[0]
            if ob.type == "MESH" and (base.startswith("LOD3_L0_e") or (base.startswith("LOD3_A") and "_e" in base)):
                ob.matrix_world = S @ ob.matrix_world
                n += 1
        check(n > 0, "no LOD 3 body objects to scale")
        bpy.context.view_layer.update()
        activate(root)
        mod = os.path.join(OUT, "mods", A.get("mod-name", "blender-car"))
        r = bpy.ops.b3.export_mod(kind="FOLDER", filepath=mod)
        ok = check("FINISHED" in r, "export the scaled %s to a mod: %s" % (props["name"], r))
        patch = os.path.join(mod, "patches", *props["source_path"].split("/")) + ".b3patch"
        ok = ok and check(os.path.isfile(patch), "no patch %s" % patch)
        if ok:
            # b3modtool's own account of it: check the same model again (nothing written)
            from b3_car_tools import gltf_settings, tool, workflow
            stem = props["sidecar"][: -len(".b3car.json")]
            model = os.path.join(props["export_dir"], gltf_settings.model_name(stem))
            res = workflow.check([p.b3modtool], A["disc"], model, os.path.join(props["export_dir"], props["sidecar"]))
            say("%s scaled 1.3x (%d objects): %s" % (props["name"], n, tool.summary_text(res)))
            check(res["edited"] >= n and res["restripped"] == 0, "the scaled objects were not edited in place")
            check(res["size_after"] == res["size_before"], "the scaled car changed size")


try:
    main()
except Exception:
    traceback.print_exc()
    fails.append("exception (above)")
if fails:
    print("blender_check(blender): FAIL " + "; ".join(fails))
    sys.stdout.flush()
    sys.exit(1)
print("blender_check(blender): PASS")
