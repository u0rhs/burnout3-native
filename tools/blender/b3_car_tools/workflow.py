# The add-on's steps as plain Python (no bpy): each runs b3modtool and returns what Blender should show, or raises
# Failed with a Problem (errors.py). The bpy layer (ops.py) does only what needs Blender: its glTF importer and
# exporter, the objects and their properties, the reports.
import os

from . import errors, tool


class Failed(Exception):
    def __init__(self, problem):
        super().__init__(problem.report())
        self.problem = problem


def _fail(res):
    raise Failed(errors.classify(res.refusal(), res.code))


def _need_disc(disc):
    if not disc or not os.path.exists(disc):
        raise Failed(errors.Problem("disc", "No disc set", repr(disc or ""),
                                    "set the disc (its folder or the disc image) in the add-on's preferences"))


def list_cars(argv, disc, run=tool.run):
    """[tool.Car] of `car list`"""
    _need_disc(disc)
    res = run(tool.cmd_car_list(argv, disc))
    if not res.ok:
        _fail(res)
    cars = tool.parse_car_list(res.out)
    if not cars:
        raise Failed(errors.Problem("disc", "No cars on this disc", disc, "is it the game's disc (or its folder)?"))
    return cars


class Export:
    def __init__(self, export_dir, sidecar, model, notes, summary):
        self.export_dir, self.sidecar, self.model, self.notes, self.summary = export_dir, sidecar, model, notes, summary


def export_car(argv, disc, car, work_dir, run=tool.run, exists=os.path.exists):
    """`car export` of one car (a tool.Car) into a new folder under `work_dir` (never over an earlier export: its paint
    PNGs may hold your edits)."""
    _need_disc(disc)
    out = tool.fresh_dir(os.path.join(work_dir, tool.safe_name(car.name or car.key)), exists)
    res = run(tool.cmd_car_export(argv, disc, car.key, out))
    if not res.ok:
        _fail(res)
    sidecar = tool.find_sidecar(out)
    if not sidecar:
        raise Failed(errors.Problem("sidecar", "The export wrote no sidecar", out, "is b3modtool from this release?"))
    model = tool.export_model(out, sidecar)
    notes = [ln[len("  note: "):] for ln in res.out.splitlines() if ln.startswith("  note: ")]
    summary = next((ln for ln in res.out.splitlines() if ln.startswith(car.key)), "")
    return Export(out, sidecar, model, notes, summary)


def check(argv, disc, model, sidecar, run=tool.run):
    """`car check`: the import's checks, nothing written. -> tool.parse_import's dict"""
    _need_disc(disc)
    res = run(tool.cmd_car_check(argv, disc, model, sidecar))
    if not res.ok:
        _fail(res)
    return tool.parse_import(res.out)


def make_mod(argv, disc, model, sidecar, out, run=tool.run):
    """`car import` into a mod. `out`: a folder (a b3patch mod: patches/ and mod.ini), a .b3mod file (the mod folder
    beside it, without the extension, then `pack`), or a .BGV / .BTV file (the whole new file, to try on your own PC).
    -> tool.parse_import's dict, with "mod_dir", "b3mod" and "verify" (verify's last line) where they apply."""
    _need_disc(disc)
    low = out.lower()
    b3mod = ""
    target = out
    if low.endswith(".b3mod"):
        b3mod, target = out, out[: -len(".b3mod")]
    res = run(tool.cmd_car_import(argv, disc, model, sidecar, target))
    if not res.ok:
        _fail(res)
    r = tool.parse_import(res.out)
    if low.endswith((".bgv", ".btv")):
        return r
    r["mod_dir"] = target
    v = run(tool.cmd_verify(argv, target, disc))
    lines = [ln for ln in v.out.splitlines() if ln.strip()]
    r["verify"] = lines[-1] if lines else v.refusal()
    if not v.ok:
        _fail(v)
    if b3mod:
        p = run(tool.cmd_pack(argv, target, b3mod, disc))
        if not p.ok:
            _fail(p)
        r["b3mod"] = b3mod
    return r
