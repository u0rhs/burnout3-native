# The Blender side of the add-on (bpy): preferences, operators, the menu entries and the sidebar panel. Kept thin:
# what b3modtool runs and what its output means is in tool.py / workflow.py / errors.py, the glTF options in
# gltf_settings.py, the car's properties in props.py (all plain Python, unit-tested without Blender).
import os

import bpy
from bpy.props import BoolProperty, EnumProperty, StringProperty

from . import errors, gltf_settings, props, tool, workflow

ADDON = __package__
ADDON_DIR = os.path.dirname(os.path.abspath(__file__))
MAX_NOTES = 8


# ---- preferences ----

class B3CarToolsPreferences(bpy.types.AddonPreferences):
    bl_idname = ADDON

    b3modtool: StringProperty(
        name="b3modtool",
        description="The port's release folder (where the launcher and b3modtool are), or the b3modtool program "
                    "itself. Empty: looked for on PATH",
        subtype="FILE_PATH")
    disc: StringProperty(
        name="Disc",
        description="Your Burnout 3 disc: the folder of its files or the disc image (.iso). Nothing is written to it",
        subtype="FILE_PATH")
    work_dir: StringProperty(
        name="Work folder",
        description="Where imported cars are exported to (one folder per car: the model, its paint PNGs and the "
                    "sidecar). These are the game's data: keep them for your own editing",
        default=os.path.join(os.path.expanduser("~"), "b3-car-work"),
        subtype="DIR_PATH")
    apply_modifiers: BoolProperty(
        name="Apply modifiers",
        description="Export the meshes with their modifiers applied (a Mirror or Subdivision modifier ends up in "
                    "the game)",
        default=True)

    def draw(self, context):
        col = self.layout.column()
        col.prop(self, "b3modtool")
        col.prop(self, "disc")
        col.prop(self, "work_dir")
        col.prop(self, "apply_modifiers")
        try:
            col.label(text="b3modtool: " + tool.find(bpy.path.abspath(self.b3modtool), ADDON_DIR), icon="CHECKMARK")
        except tool.ToolMissing as e:
            col.label(text=str(e)[:110], icon="ERROR")


def prefs(context):
    return context.preferences.addons[ADDON].preferences


def tool_argv(context):
    return [tool.find(bpy.path.abspath(prefs(context).b3modtool), ADDON_DIR)]


def disc_of(context, given=""):
    return bpy.path.abspath(given or prefs(context).disc)


# ---- the car in the scene ----

def car_root(context):
    """The root empty of the car the user is working on: the active object's (or its parents'), else the one in the
    active collection."""
    ob = context.active_object
    while ob is not None:
        if props.KEY in ob:
            return ob
        ob = ob.parent
    coll = context.view_layer.active_layer_collection.collection
    for ob in coll.all_objects:
        if props.KEY in ob:
            return ob
    return None


def car_props(root):
    p = root[props.KEY]
    return p.to_dict() if hasattr(p, "to_dict") else dict(p)


def find_layer_collection(lc, coll):
    if lc.collection == coll:
        return lc
    for c in lc.children:
        r = find_layer_collection(c, coll)
        if r:
            return r
    return None


def save_edited_paints(export_dir):
    """Paint pictures painted on in Blender (Texture Paint) and not saved: saved over their PNGs in the export folder,
    where b3modtool reads them."""
    saved = []
    root = os.path.normcase(os.path.abspath(export_dir))
    for img in bpy.data.images:
        if not img.is_dirty or not img.filepath or img.packed_file:
            continue
        p = os.path.normcase(os.path.abspath(bpy.path.abspath(img.filepath)))
        if os.path.dirname(p) == root:
            img.save()
            saved.append(os.path.basename(p))
    return saved


def export_glb(context, root, path):
    """The car's collection through Blender's glTF exporter into `path` (.glb). -> [warnings]"""
    for ob in context.objects_in_mode_unique_data:   # Edit Mode changes into the mesh data
        ob.update_from_editmode()
    coll = root.users_collection[0]
    vl = context.view_layer
    lc = find_layer_collection(vl.layer_collection, coll)
    if lc is None:
        raise workflow.Failed(errors.Problem("scene", "The car's collection is not in this view layer", coll.name,
                                             "include the collection (its checkbox in the Outliner)"))
    available = set(bpy.ops.export_scene.gltf.get_rna_type().properties.keys())
    kw, warnings = gltf_settings.export_kwargs(available, path, prefs(context).apply_modifiers)
    prev = vl.active_layer_collection
    vl.active_layer_collection = lc
    try:
        if kw.get("use_selection"):
            for ob in context.view_layer.objects:
                ob.select_set(ob.name in coll.all_objects)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        r = bpy.ops.export_scene.gltf(**kw)
    finally:
        vl.active_layer_collection = prev
    if "FINISHED" not in r:
        raise workflow.Failed(errors.Problem("export", "Blender's glTF export did not finish", path, ""))
    return warnings


def prepare(context, op):
    """(argv, disc, props, model, sidecar) for check / export: the car's glTF written beside its sidecar."""
    root = car_root(context)
    if root is None:
        raise workflow.Failed(errors.Problem("scene", "No car selected", "select an object of a car imported with "
                                             "File > Import > Burnout 3 car", ""))
    p = car_props(root)
    try:
        side, restored = props.ensure_sidecar(p)
    except props.PropsError as e:
        raise workflow.Failed(errors.Problem("sidecar", "The car's properties are incomplete", str(e), ""))
    if restored:
        op.report({"WARNING"}, "The export folder had no sidecar: written back from the .blend (%s); paint pictures "
                               "that are not there stay as they are" % side)
    saved = save_edited_paints(p["export_dir"])
    if saved:
        op.report({"INFO"}, "Saved the edited paint picture(s) " + ", ".join(saved))
    stem = os.path.basename(side)[: -len(".b3car.json")]
    model = os.path.join(p["export_dir"], gltf_settings.model_name(stem))
    for w in export_glb(context, root, model):
        op.report({"WARNING"}, "This Blender's glTF exporter: " + w)
    # the preferences' disc when it is set (a disc that moved), else the one the car was imported from
    disc = disc_of(context, "" if prefs(context).disc else p.get("disc", ""))
    return tool_argv(context), disc, p, model, side


def show(op, r):
    notes = r.get("notes", [])
    for n in notes[:MAX_NOTES]:
        op.report({"INFO"}, n)
    if len(notes) > MAX_NOTES:
        op.report({"INFO"}, "(%d more notes: see the system console)" % (len(notes) - MAX_NOTES))
    for n in notes:
        print("b3_car_tools: note:", n)


def fail(op, e):
    if isinstance(e, workflow.Failed):
        pr = e.problem
        print("b3_car_tools:", pr.kind, "--", pr.detail)
        op.report({"ERROR"}, pr.report())
        if pr.objects:
            for name in pr.objects:
                ob = bpy.data.objects.get(name)
                if ob is not None:
                    ob.select_set(True)
    else:
        op.report({"ERROR"}, str(e))
    return {"CANCELLED"}


# ---- import ----

_cars = {}   # disc -> [tool.Car]


def cars_for(context, disc):
    if disc not in _cars:
        _cars[disc] = workflow.list_cars(tool_argv(context), disc)
    return _cars[disc]


class B3_OT_import_car(bpy.types.Operator):
    """Export a car from your disc with b3modtool and import it, named the game's way, into its own collection"""
    bl_idname = "b3.import_car"
    bl_label = "Import Burnout 3 Car"
    bl_options = {"REGISTER", "UNDO"}

    def _search(self, context, edit_text):
        try:
            cars = cars_for(context, disc_of(context, self.disc))
        except (workflow.Failed, tool.ToolMissing):
            return []
        t = edit_text.upper()
        return [(c.name or c.key, "%s  %s" % (c.kind, c.key)) for c in cars if t in (c.name + " " + c.key).upper()]

    disc: StringProperty(name="Disc", description="Empty: the add-on preferences' disc", subtype="FILE_PATH")
    car: StringProperty(name="Car", description="A car by its name, its number in the game's list or its path "
                                                "(PVEH/COMP/CAR1.BGV)", search=_search)
    work_dir: StringProperty(name="Work folder", description="Empty: the add-on preferences' work folder",
                             subtype="DIR_PATH")

    def invoke(self, context, event):
        return context.window_manager.invoke_props_dialog(self, width=480)

    def draw(self, context):
        col = self.layout.column()
        col.prop(self, "car")
        col.prop(self, "disc", text="Disc (empty: preferences)")

    def execute(self, context):
        try:
            argv = tool_argv(context)
            disc = disc_of(context, self.disc)
            car = tool.resolve_car(cars_for(context, disc), self.car)
            if car is None:
                raise workflow.Failed(errors.Problem("car", "No such car", repr(self.car),
                                                     "pick one from the list (type a few letters of its name)"))
            work = bpy.path.abspath(self.work_dir or prefs(context).work_dir)
            ex = workflow.export_car(argv, disc, car, work)
            with open(ex.sidecar, encoding="utf-8") as f:
                side_text = f.read()
            # a collection of its own, active while the importer runs; the objects it made are moved there (Blender
            # 4.2's importer links them to the scene's own collection, whichever is active)
            coll = bpy.data.collections.new("B3 " + (car.name or car.key))
            context.scene.collection.children.link(coll)
            vl = context.view_layer
            prev = vl.active_layer_collection
            vl.active_layer_collection = find_layer_collection(vl.layer_collection, coll)
            before = set(bpy.data.objects)
            try:
                available = set(bpy.ops.import_scene.gltf.get_rna_type().properties.keys())
                r = bpy.ops.import_scene.gltf(**gltf_settings.import_kwargs(available, ex.model))
            finally:
                vl.active_layer_collection = prev
            if "FINISHED" not in r:
                raise workflow.Failed(errors.Problem("import", "Blender's glTF import did not finish", ex.model, ""))
            for ob in set(bpy.data.objects) - before:
                if coll not in ob.users_collection:
                    coll.objects.link(ob)
                for c in list(ob.users_collection):
                    if c != coll:
                        c.objects.unlink(ob)
            root = None
            for ob in coll.all_objects:
                b3 = ob.get("b3")
                b3 = b3.to_dict() if hasattr(b3, "to_dict") else b3
                if ob.parent is None and props.is_root_extras(b3):
                    root = ob
            if root is None:
                raise workflow.Failed(errors.Problem("import", "The imported car has no root object", ex.model, ""))
            root[props.KEY] = props.car_props(side_text, ex.export_dir, os.path.basename(ex.sidecar),
                                              os.path.basename(ex.model), disc)
            coll[props.KEY + "_root"] = root.name
        except (workflow.Failed, tool.ToolMissing, props.PropsError, OSError) as e:
            return fail(self, e)
        self.report({"INFO"}, "%s imported (exported into %s)" % (car.label, ex.export_dir))
        return {"FINISHED"}


# ---- check / export ----

class B3_OT_check_car(bpy.types.Operator):
    """Run b3modtool's import checks on the car as it is now (nothing written but the glTF beside the export)"""
    bl_idname = "b3.check_car"
    bl_label = "Check Car"

    def execute(self, context):
        try:
            argv, disc, p, model, side = prepare(context, self)
            r = workflow.check(argv, disc, model, side)
        except (workflow.Failed, tool.ToolMissing) as e:
            return fail(self, e)
        show(self, r)
        self.report({"INFO"}, "Check OK: " + tool.summary_text(r))
        return {"FINISHED"}


_last_mod = {"dir": ""}


class B3_OT_export_mod(bpy.types.Operator):
    """Export the car with Blender's glTF exporter and make a mod with b3modtool: a patch mod folder, a .b3mod, or the
    whole car file"""
    bl_idname = "b3.export_mod"
    bl_label = "Export Burnout 3 Car Mod"

    kind: EnumProperty(
        name="Make",
        items=[("FOLDER", "Mod folder", "A mod folder (mod.ini and patches/ with the car's b3patch: only your changes)"),
               ("B3MOD", ".b3mod file", "The mod folder, packed into one .b3mod file to share"),
               ("FILE", "Car file", "The whole new car file (.BGV), to try on your own PC: never share it")],
        default="FOLDER")
    filepath: StringProperty(name="Where", description="The mod folder, the .b3mod file or the .BGV file",
                             subtype="FILE_PATH")

    def invoke(self, context, event):
        root = car_root(context)
        if root is not None and not self.filepath:
            name = tool.safe_name(car_props(root).get("name", "car"))
            self.filepath = os.path.join(bpy.path.abspath(prefs(context).work_dir), "mods", name + "-mod")
        return context.window_manager.invoke_props_dialog(self, width=520)

    def execute(self, context):
        try:
            out = bpy.path.abspath(self.filepath)
            if not out:
                raise workflow.Failed(errors.Problem("mod", "No place for the mod", "", "set Where"))
            base = out[:-1] if out.endswith(("/", "\\")) else out
            low = base.lower()
            if self.kind == "B3MOD" and not low.endswith(".b3mod"):
                base += ".b3mod"
            elif self.kind == "FILE" and not low.endswith((".bgv", ".btv")):
                root = car_root(context)   # the car file's own extension (.BGV, or .BTV for a traffic vehicle)
                src = car_props(root).get("source_path", "") if root is not None else ""
                base += os.path.splitext(src)[1] or ".BGV"
            argv, disc, p, model, side = prepare(context, self)
            r = workflow.make_mod(argv, disc, model, side, base)
        except (workflow.Failed, tool.ToolMissing) as e:
            return fail(self, e)
        show(self, r)
        _last_mod["dir"] = r.get("mod_dir", os.path.dirname(base))
        where = r.get("b3mod") or r.get("mod_dir") or base
        self.report({"INFO"}, "Mod written: %s (%s)%s" % (where, tool.summary_text(r),
                                                          "; " + r["verify"] if r.get("verify") else ""))
        return {"FINISHED"}


class B3_OT_open_folder(bpy.types.Operator):
    """Open a folder in the system's file manager"""
    bl_idname = "b3.open_folder"
    bl_label = "Open Folder"

    which: EnumProperty(items=[("MOD", "Mod folder", ""), ("EXPORT", "Export folder", "")], default="MOD")

    def execute(self, context):
        if self.which == "MOD":
            d = _last_mod["dir"] or os.path.join(bpy.path.abspath(prefs(context).work_dir), "mods")
        else:
            root = car_root(context)
            d = car_props(root).get("export_dir", "") if root is not None else ""
        if not d or not os.path.isdir(d):
            self.report({"WARNING"}, "No such folder yet: %s" % (d or "(none)"))
            return {"CANCELLED"}
        bpy.ops.wm.path_open(filepath=d)
        return {"FINISHED"}


# ---- UI ----

class B3_PT_car(bpy.types.Panel):
    bl_label = "Burnout 3 Car"
    bl_space_type = "VIEW_3D"
    bl_region_type = "UI"
    bl_category = "B3 Car"

    def draw(self, context):
        col = self.layout.column(align=True)
        col.operator(B3_OT_import_car.bl_idname, icon="IMPORT", text="Import Car...")
        root = car_root(context)
        if root is None:
            col.label(text="Select an object of an imported car")
            return
        p = car_props(root)
        box = self.layout.box()
        box.label(text=p.get("name", "?"), icon="AUTO")
        box.label(text=p.get("source_path", ""))
        col = self.layout.column(align=True)
        col.operator(B3_OT_check_car.bl_idname, icon="CHECKMARK", text="Check")
        col.operator(B3_OT_export_mod.bl_idname, icon="EXPORT", text="Export to Mod...")
        row = self.layout.row(align=True)
        row.operator(B3_OT_open_folder.bl_idname, text="Export Folder", icon="FILE_FOLDER").which = "EXPORT"
        row.operator(B3_OT_open_folder.bl_idname, text="Mod Folder", icon="FILE_FOLDER").which = "MOD"


def menu_import(self, context):
    self.layout.operator(B3_OT_import_car.bl_idname, text="Burnout 3 Car (b3modtool)")


def menu_export(self, context):
    self.layout.operator(B3_OT_export_mod.bl_idname, text="Burnout 3 Car Mod (b3modtool)")


CLASSES = (B3CarToolsPreferences, B3_OT_import_car, B3_OT_check_car, B3_OT_export_mod, B3_OT_open_folder, B3_PT_car)


def register():
    for c in CLASSES:
        bpy.utils.register_class(c)
    bpy.types.TOPBAR_MT_file_import.append(menu_import)
    bpy.types.TOPBAR_MT_file_export.append(menu_export)


def unregister():
    bpy.types.TOPBAR_MT_file_export.remove(menu_export)
    bpy.types.TOPBAR_MT_file_import.remove(menu_import)
    for c in reversed(CLASSES):
        bpy.utils.unregister_class(c)
    _cars.clear()
