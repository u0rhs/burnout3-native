# B3 Car Tools: a Blender add-on for Burnout 3 car mods, a front end on the port's b3modtool (docs/modding/
# tutorials/09-car-mods-in-blender.md; docs/modding/DESIGN.md §11). Blender 4.2 LTS and newer; an extension (its
# blender_manifest.toml) or a legacy add-on (bl_info) from the same zip (`make blender-addon`).
#
#   File > Import > Burnout 3 Car     `b3modtool car export` of a car from your disc, then Blender's own glTF importer
#   sidebar (N) > B3 Car > Check      Blender's glTF exporter, then `b3modtool car check` (nothing written)
#   File > Export > Burnout 3 Car Mod the same, then `b3modtool car import` into a patch mod (and `pack` for a .b3mod)
#
# All file-format work is b3modtool's. The modules without bpy (tool, errors, props, gltf_settings, workflow) are
# tested by tools/blender/test_b3_car_tools.py with plain python3; tools/modding/blender_check.sh runs the add-on in a
# headless Blender.
#
# Part of the Burnout 3 port's mod kit; MIT licence (LICENSE at the repository's root), which is compatible with the
# GPL that Blender add-ons run under.

bl_info = {
    "name": "B3 Car Tools",
    "author": "Burnout 3 port mod kit",
    "version": (0, 1, 0),
    "blender": (4, 2, 0),
    "location": "File > Import / Export > Burnout 3 Car; 3D View sidebar > B3 Car",
    "description": "Import Burnout 3 cars from your disc and export them as mods, through b3modtool",
    "category": "Import-Export",
}

try:
    import bpy  # noqa: F401
except ImportError:   # the unit tests import the plain modules without Blender
    bpy = None


def register():
    from . import ops
    ops.register()


def unregister():
    from . import ops
    ops.unregister()
