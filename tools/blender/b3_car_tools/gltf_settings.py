# The options the add-on gives Blender's own glTF importer and exporter. Plain Python (no bpy): the bpy layer passes
# the names of the options the running Blender has (the operator's RNA properties), so one table serves Blender 4.2
# LTS and later, and an option a version does not have is left out (and reported when it matters).
#
# What Blender's glTF add-on does to a car, read from its source (glTF-Blender-IO, Blender 4.2; docs/modding/b3modtool.md
# "Blender"), and why each option is set:
#   import: one Blender vertex per glTF vertex the triangles use (merge_vertices off: merged vertices would lose the
#     export's split UVs); NORMAL as custom split normals (import_shading NORMALS); the "_" attributes as mesh
#     attributes when they are floats (b3modtool writes them as one float SCALAR per component, _B3_NRM_X .. _Z,
#     _B3_INFL_0 / _1, _B3_W: Blender 4.2's importer matches a mesh's "_" attributes with their data in a set's order,
#     so attributes of different widths fail at random, docs/modding/b3modtool.md "Blender"); extras as custom
#     properties; node matrices as location / rotation / scale; textures from the export folder, not packed, so a
#     paint PNG edited outside Blender shows after Image > Reload.
#   export: one glTF vertex per distinct corner record, in Blender's own order (b3modtool's import does not depend on
#     the order); Y up; NORMAL rounded to 4 decimals (b3modtool keeps the stored normal bytes while NORMAL points the
#     same way); texture coordinates as they are (v <-> 1 - v is exact for the export's values); the "_" attributes
#     (export_attributes); extras (export_extras: the mesh's "b3" names its entry); no Draco (it would quantise
#     positions and UVs), no shared accessors, no loose edges / points (not triangles), no materials or images (the
#     import keeps the game's materials; placeholders keep one primitive per material slot), no animation,
#     skins, morph targets, cameras or lights; only the car's collection.

IMPORT_WANT = {
    "merge_vertices": False,
    "import_shading": "NORMALS",
    "import_pack_images": False,
    "guess_original_bind_pose": False,
    "import_select_created_objects": True,
    "import_scene_extras": True,
}

EXPORT_WANT = {
    "export_format": "GLB",
    "use_selection": False,
    "use_visible": False,
    "use_renderable": False,
    "use_active_collection": True,
    "use_active_collection_with_nested": True,
    "use_active_scene": False,
    "export_extras": True,
    "export_yup": True,
    "export_texcoords": True,
    "export_normals": True,
    "export_attributes": True,
    "export_tangents": False,
    "export_materials": "PLACEHOLDER",
    "export_draco_mesh_compression_enable": False,
    "export_shared_accessors": False,
    "export_loose_edges": False,
    "export_loose_points": False,
    "export_gpu_instances": False,
    "export_animations": False,
    "export_skins": False,
    "export_morph": False,
    "export_cameras": False,
    "export_lights": False,
    "export_hierarchy_flatten_objs": False,
    "export_hierarchy_full_collections": False,
    "will_save_settings": False,
    "check_existing": False,
}

# without these the round trip loses data or exports more than the car; a Blender without one gets a warning
EXPORT_NEEDED = {
    "export_attributes": "the _B3_NRM_* / _B3_INFL_* attributes (stored normals, damage influences) are not exported: "
                         "the import makes them from NORMAL and the nearest original vertex",
    "export_extras": "the meshes' extras are not exported: the import finds the parts by their names",
    "use_active_collection": "the car's collection cannot be chosen: everything in the scene is exported",
}


def import_kwargs(available, filepath):
    """kwargs for bpy.ops.import_scene.gltf"""
    kw = {k: v for k, v in IMPORT_WANT.items() if k in available}
    kw["filepath"] = filepath
    return kw


def export_kwargs(available, filepath, apply_modifiers=True):
    """(kwargs for bpy.ops.export_scene.gltf, [warnings]). `apply_modifiers`: the evaluated meshes (a Mirror or
    Subdivision modifier ends up in the game) or the meshes as they are."""
    want = dict(EXPORT_WANT)
    want["export_apply"] = bool(apply_modifiers)
    if "use_active_collection" not in available and "use_selection" in available:
        want["use_selection"] = True   # an old Blender: the add-on selects the car's objects instead
    kw = {k: v for k, v in want.items() if k in available}
    kw["filepath"] = filepath
    warnings = [msg for k, msg in EXPORT_NEEDED.items() if k not in available]
    return kw, warnings


def model_name(stem):
    """The file the add-on exports beside the sidecar (never over the export's own <stem>.gltf)."""
    return stem + "_blender.glb"
