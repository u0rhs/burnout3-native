# b3modtool from the add-on: finding the program, its command lines, running it, and reading what it prints.
# Plain Python (no bpy): unit-tested by tools/blender/test_b3_car_tools.py with the system python3.
#
# The add-on is a front end: every file format is b3modtool's (docs/modding/b3modtool.md). This module only knows the
# commands and the shape of their output:
#   car list <disc>                                  the cars (index, path, class, name), then the traffic vehicles
#   car export <disc> <car> <dir>                    the glTF, its .bin, the paint PNGs and the .b3car.json sidecar
#   car check <disc> <model> --sidecar <file>        the import's checks, nothing written
#   car import <disc> <model> <mod dir> --sidecar <file>   a b3patch mod (patches/PVEH/../CARn.BGV.b3patch, mod.ini)
#   pack <mod dir> <out.b3mod> --game <disc>         the mod as one .b3mod file
# A refusal is one line on stderr, "b3modtool: <what>", and exit status 1 (errors.py turns it into a Blender report).
import os
import re
import shutil
import subprocess
import sys

EXE = "b3modtool.exe" if sys.platform.startswith("win") else "b3modtool"


class ToolMissing(Exception):
    pass


def candidates(setting="", addon_dir="", env=None, which=shutil.which):
    """Where b3modtool may be, in order: the preference (the program itself, or a folder: the release package's
    folder, or a source checkout and its build/), $B3MODTOOL, a source checkout around the add-on (tools/blender/..),
    then PATH."""
    env = os.environ if env is None else env
    out = []

    def folder(d):
        if d:
            out.extend([os.path.join(d, EXE), os.path.join(d, "build", EXE)])
            if EXE != "b3modtool":
                out.append(os.path.join(d, "b3modtool"))

    for s in (setting, env.get("B3MODTOOL", "")):
        s = os.path.expanduser(s.strip()) if s else ""
        if not s:
            continue
        if os.path.isdir(s):
            folder(s)
        else:
            out.append(s)
    if addon_dir:   # tools/blender/b3_car_tools in a checkout: <repo>/build/b3modtool
        repo = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(addon_dir))))
        out.append(os.path.join(repo, "build", EXE))
    w = which(EXE) or which("b3modtool")
    if w:
        out.append(w)
    return out


def find(setting="", addon_dir="", env=None, which=shutil.which, isfile=os.path.isfile):
    for c in candidates(setting, addon_dir, env, which):
        if isfile(c):
            return c
    raise ToolMissing(
        "b3modtool was not found: set its place in the add-on's preferences (Edit > Preferences > Add-ons > "
        "B3 Car Tools): the folder of the port's release (where the launcher and %s are) or the program itself" % EXE)


# ---- command lines (lists: no shell, so spaces and quotes in paths need nothing) ----

def cmd_car_list(tool, disc):
    return [*tool, "car", "list", disc]


def cmd_car_export(tool, disc, car_key, out_dir):
    return [*tool, "car", "export", disc, car_key, out_dir]


def cmd_car_check(tool, disc, model, sidecar):
    return [*tool, "car", "check", disc, model, "--sidecar", sidecar]


def cmd_car_import(tool, disc, model, sidecar, out):
    """`out`: a mod folder (a b3patch mod), or a path ending in .BGV / .BTV (the whole file, for a quick try)."""
    return [*tool, "car", "import", disc, model, out, "--sidecar", sidecar]


def cmd_pack(tool, mod_dir, out_file, disc=""):
    c = [*tool, "pack", mod_dir, out_file]
    return c + ["--game", disc] if disc else c


def cmd_verify(tool, mod, disc=""):
    c = [*tool, "verify", mod]
    return c + ["--game", disc] if disc else c


class Result:
    def __init__(self, code, out, err):
        self.code, self.out, self.err = code, out, err

    @property
    def ok(self):
        return self.code == 0

    def refusal(self):
        """b3modtool's own message (the "b3modtool: " lines of stderr), or the last line of whatever it printed."""
        lines = [ln[len("b3modtool: "):] for ln in self.err.splitlines() if ln.startswith("b3modtool: ")]
        if lines:
            return " ".join(lines)
        rest = [ln for ln in (self.err + "\n" + self.out).splitlines() if ln.strip()]
        return rest[-1].strip() if rest else "b3modtool stopped (exit status %d) without a message" % self.code


def run(cmd, timeout=600, runner=subprocess.run):
    """Runs one command; never raises for its exit status (Result.ok)."""
    kw = {}
    if sys.platform.startswith("win"):
        kw["creationflags"] = getattr(subprocess, "CREATE_NO_WINDOW", 0)   # no console window from Blender
    try:
        p = runner(cmd, capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=timeout, **kw)
    except FileNotFoundError:
        raise ToolMissing("cannot start %s" % cmd[0])
    except subprocess.TimeoutExpired:
        return Result(124, "", "b3modtool: did not finish in %d s" % timeout)
    return Result(p.returncode, p.stdout or "", p.stderr or "")


# ---- reading the output ----

class Car:
    def __init__(self, index, key, kind, name):
        self.index, self.key, self.kind, self.name = index, key, kind, name

    @property
    def label(self):
        return self.name or self.key

    def __repr__(self):
        return "Car(%r, %r, %r, %r)" % (self.index, self.key, self.kind, self.name)


_PLAYER = re.compile(r"^\s*(\d+)\s+(PVEH/\S+)\s+(\S+)(?:\s+(.*?))?\s*$")
_TRAFFIC = re.compile(r"^\s+(PVEH/\S+\.BTV)\s*$", re.I)


def parse_car_list(text):
    """`car list`'s output -> [Car]: the player cars in the game's order (index >= 0), then the traffic vehicles
    (index -1, kind "Traffic", no name)."""
    cars, traffic = [], False
    for ln in text.splitlines():
        if ln.startswith("traffic and other vehicles"):
            traffic = True
            continue
        if traffic:
            m = _TRAFFIC.match(ln)
            if m:
                cars.append(Car(-1, m.group(1), "Traffic", ""))
            continue
        m = _PLAYER.match(ln)
        if m:
            cars.append(Car(int(m.group(1)), m.group(2), m.group(3), m.group(4) or ""))
    return cars


def resolve_car(cars, text):
    """A car by its name, its number in the list or its path (as `car export` takes them), or None."""
    t = text.strip().upper().replace("\\", "/")
    if not t:
        return None
    for c in cars:
        if t in (c.key.upper(), c.name.upper()) or (c.index >= 0 and t == str(c.index)):
            return c
    k = t if t.startswith("PVEH/") else "PVEH/" + t   # COMP/CAR1, COMP/CAR1.BGV
    for c in cars:
        if c.key.upper() in (k, k + ".BGV"):
            return c
    return None


_SUMMARY = re.compile(
    r"^car (import|check): (\S+): (\d+) meshes \((\d+) unchanged, (\d+) edited in place, (\d+) with new triangles, "
    r"(\d+) not in the model: kept\), (\d+) packets moved, (\d+) paints changed( \(re-quantised together\))?; "
    r"(\d+) -> (\d+) bytes(?: -> (.*)|; ok, nothing written)\s*$")


def parse_import(text):
    """`car import` / `car check` output -> {"notes": [...], and from its summary line: "car", "meshes", "unchanged",
    "edited", "restripped", "kept", "moved", "paints", "requantised", "size_before", "size_after", "written"} (the
    summary keys are missing when there was no summary line)."""
    r = {"notes": []}
    for ln in text.splitlines():
        if ln.startswith("  note: "):
            r["notes"].append(ln[len("  note: "):])
            continue
        m = _SUMMARY.match(ln)
        if m:
            g = m.groups()
            r.update(command=g[0], car=g[1], meshes=int(g[2]), unchanged=int(g[3]), edited=int(g[4]),
                     restripped=int(g[5]), kept=int(g[6]), moved=int(g[7]), paints=int(g[8]),
                     requantised=bool(g[9]), size_before=int(g[10]), size_after=int(g[11]), written=g[12] or "")
    return r


def summary_text(r):
    """One line for Blender's status bar."""
    if "meshes" not in r:
        return ""
    parts = ["%d meshes: %d unchanged, %d edited in place, %d with new triangles" %
             (r["meshes"], r["unchanged"], r["edited"], r["restripped"])]
    if r["kept"]:
        parts.append("%d not exported (kept as they are)" % r["kept"])
    if r["paints"]:
        parts.append("%d paint(s) changed" % r["paints"])
    grow = r["size_after"] - r["size_before"]
    parts.append("%d bytes (%s)" % (r["size_after"], "%+d" % grow if grow else "same size"))
    return "; ".join(parts)


def find_sidecar(export_dir):
    """The one *.b3car.json of an export folder ("" when none or several)."""
    try:
        found = [f for f in os.listdir(export_dir) if f.lower().endswith(".b3car.json")]
    except OSError:
        return ""
    return os.path.join(export_dir, found[0]) if len(found) == 1 else ""


def export_model(export_dir, sidecar):
    """The export's own glTF beside its sidecar: <stem>.gltf"""
    stem = os.path.basename(sidecar)[: -len(".b3car.json")]
    return os.path.join(export_dir, stem + ".gltf")


def fresh_dir(base, exists=os.path.exists):
    """`base`, or base_2, base_3 ... the first that does not exist (an export never writes over an earlier one: its
    paint pictures may hold your edits)."""
    if not exists(base):
        return base
    k = 2
    while exists("%s_%d" % (base, k)):
        k += 1
    return "%s_%d" % (base, k)


def safe_name(s):
    """A file / folder name from a car's name: letters, digits, '-', '_'."""
    t = re.sub(r"[^A-Za-z0-9_-]+", "-", s).strip("-").lower()
    return t or "car"
