# The export's sidecar <-> the custom properties the add-on keeps on the car's root object. Plain Python (no bpy).
#
# `car export` writes, beside the model, <stem>.b3car.json: which file of the disc the car came from (path, size,
# FNV-1a 64) and everything b3modtool's import needs (docs/modding/b3modtool.md "The export's files"). The add-on keeps
# a copy of it on the car's root empty, as the custom property "b3_car" (a group of strings and small numbers), with
# where the export lives. So a saved .blend knows its car: the export (glTF + sidecar) and mod steps find their files
# from it, and when the export folder's sidecar was deleted or the folder moved, the copy is written back (the paint
# PNGs are not in the .blend: without them the paints stay as they are, and b3modtool says so).
# glTF's own extras come through Blender's importer as custom properties too: the root's "b3" (file, stem, sidecar),
# each object's "b3" (its part, wheel or entry) and each mesh's "b3" (LOD, slot, entry, material words); the exporter
# writes them back as extras when Custom Properties are on.
import json
import os

KEY = "b3_car"            # the root object's property
FORMAT = "b3_car_tools 1"
SIDECAR_FORMAT = "b3modtool car export"


class PropsError(Exception):
    pass


def read_sidecar(text):
    """The sidecar's JSON (a dict), checked to be a car export's."""
    try:
        d = json.loads(text)
    except ValueError as e:
        raise PropsError("the sidecar is not JSON (%s)" % e)
    if not isinstance(d, dict) or d.get("format") != SIDECAR_FORMAT:
        raise PropsError("not a car export's sidecar (its format is not \"%s\")" % SIDECAR_FORMAT)
    src = d.get("source")
    if not isinstance(src, dict) or not all(k in src for k in ("path", "size", "fnv1a64")):
        raise PropsError("the sidecar names no source file")
    return d


def car_props(sidecar_text, export_dir, sidecar_name, model_name, disc):
    """The "b3_car" group for a car just exported into `export_dir` from `disc`. Only strings and ints (Blender's ID
    properties: ints are 32-bit; the hash stays the sidecar's hex string)."""
    d = read_sidecar(sidecar_text)
    src = d["source"]
    tex = d.get("texture") or {}
    files = tex.get("files") if isinstance(tex, dict) else None
    return {
        "format": FORMAT,
        "name": str(d.get("name") or src["path"]),
        "source_path": str(src["path"]),
        "source_size": int(src["size"]),
        "source_fnv1a64": str(src["fnv1a64"]),
        "paints": len(files) if isinstance(files, list) else 0,
        "disc": str(disc),
        "export_dir": os.path.abspath(export_dir),
        "sidecar": str(sidecar_name),
        "model": str(model_name),
        "sidecar_json": sidecar_text,
    }


def check(props):
    """Raises PropsError when `props` is not a whole "b3_car" group."""
    if not props or props.get("format") != FORMAT:
        raise PropsError("this is not a car imported by the add-on (its root has no \"%s\" property): import the car "
                         "with File > Import > Burnout 3 car" % KEY)
    for k in ("source_path", "export_dir", "sidecar", "sidecar_json"):
        if not props.get(k):
            raise PropsError("the car's \"%s\" property has no %s" % (KEY, k))


def sidecar_path(props):
    return os.path.join(props["export_dir"], props["sidecar"])


def ensure_sidecar(props, exists=os.path.exists, write=None):
    """The export's sidecar file, written back from the copy in the properties when it is not there (returns the
    path, and whether it was written). `write(path, text)` defaults to a plain file write."""
    check(props)
    p = sidecar_path(props)
    if exists(p):
        return p, False
    read_sidecar(props["sidecar_json"])
    if write is None:
        def write(path, text):
            os.makedirs(os.path.dirname(path), exist_ok=True)
            with open(path, "w", encoding="utf-8", newline="\n") as f:
                f.write(text)
    write(p, props["sidecar_json"])
    return p, True


def same_source(props, sidecar_text):
    """Whether a sidecar on disk is the one this car was exported with (the same source file)."""
    d = read_sidecar(sidecar_text)
    s = d["source"]
    return (str(s["path"]) == props.get("source_path") and int(s["size"]) == props.get("source_size")
            and str(s["fnv1a64"]) == props.get("source_fnv1a64"))


def is_root_extras(b3):
    """The glTF root node's extras as Blender's importer kept them (the root's "b3": file, stem, sidecar)."""
    return isinstance(b3, dict) and "file" in b3 and "sidecar" in b3
