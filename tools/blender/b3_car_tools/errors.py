# b3modtool's refusals as Blender reports: what went wrong, and what to do about it in Blender. Plain Python (no bpy).
# The messages are b3modtool's (src/b3modtool/carimport.cpp, tools/modding/b3modtool.cpp); each is matched by a few
# words of it, and anything not known is shown as it is.
import re


class Problem:
    def __init__(self, kind, title, detail, hint, objects=()):
        self.kind, self.title, self.detail, self.hint, self.objects = kind, title, detail, hint, list(objects)

    def report(self):
        """One line for Blender's report (the status bar and the Info editor)."""
        s = "%s: %s" % (self.title, self.detail)
        return s + (" -- " + self.hint if self.hint else "")

    def __repr__(self):
        return "Problem(%r, %r)" % (self.kind, self.detail)


# (kind, words in the message, title, hint)
_RULES = [
    ("size", "over the limit of",
     "The car is too big for the game",
     "remove detail: the lower levels of detail (LOD0 .. LOD2) are only seen from far away, and a flat area needs few "
     "triangles"),
    ("uv", "texture coordinates outside 0 .. 16",
     "UVs off the texture",
     "keep that object's UVs on the car's texture (the UV Editor's 0..1 square; the game cannot store more than 16)"),
    ("foreign", "is not one of the car's meshes",
     "An object that is not part of the car",
     "the import can only change the car's own objects: join the new part into one of them (select it, then the car's "
     "object, Ctrl J) or move it out of the car's collection"),
    ("foreign", "which this car's file does not have",
     "An object named after a part this car does not have",
     "rename it back, or join it into one of the car's objects (Ctrl J)"),
    ("twice", "stand for",
     "Two objects for one part of the car",
     "keep one object per part: join the copies (Ctrl J); the four wheels share one mesh, edit it once"),
    ("twice", "given twice",
     "Two objects for one part of the car",
     "keep one object per part: join the copies (Ctrl J)"),
    ("position", "vertex position outside",
     "A vertex far away from the car",
     "a vertex is more than 1 km out (or not a number): look for a stray vertex (Edit Mode, select all, frame it)"),
    ("triangles", "is not triangles",
     "Not a triangle mesh",
     "the game draws triangles only: leave Loose Edges and Loose Points off in the export"),
    ("uvmap", "no texture coordinates",
     "An object without a UV map",
     "every vertex needs its place on the car's texture: give the object a UV map"),
    ("paint", "the size cannot change",
     "A paint picture of another size",
     "save the paint PNG at the size it was exported at (and keep its alpha: it is a mask the game uses)"),
    ("paint", "share one CLUT",
     "Two paints that must change together",
     "the file keeps these paints in one palette: make the same change to both pictures"),
    ("zone", "is not one the file uses",
     "A damage influence the car does not have",
     "the _B3_INFL_0 / _B3_INFL_1 attributes were changed: delete them and the import takes each vertex's from the "
     "nearest original vertex"),
    ("other_file", "the export was made from another",
     "This car came from another version of the file",
     "import the car again from this disc (the one in the add-on's preferences)"),
    ("sidecar", "cannot read the sidecar",
     "The export's sidecar cannot be read",
     "import the car again"),
    ("sidecar", ".b3car.json",
     "The export's sidecar is missing",
     "the export folder must keep the .b3car.json the car was exported with; import the car again if it is gone"),
    ("disc", "not a disc folder or image",
     "The disc was not found",
     "set the disc (its folder or the disc image) in the add-on's preferences"),
    ("disc", " has no PVEH/",
     "That car is not on this disc",
     "set the disc the car was exported from"),
    ("mod", "a mod has a file or its patch",
     "The mod folder already holds the car file itself",
     "pick an empty folder for the mod, or remove its data/ copy of the car"),
    ("mod", "would replace the disc's own file",
     "The output is the disc's own file",
     "pick another place for the new file"),
    ("writer", "do not read back as meant",
     "b3modtool could not write this car",
     "this is b3modtool's error, not yours: please report it with the .blend"),
    ("tool", "unknown option",
     "This b3modtool is too old for the add-on",
     "use the b3modtool of the same release as the add-on"),
]

_OBJECT = re.compile(r'^"?([A-Za-z0-9_.]+)"?: ')


def classify(message, code=1):
    """b3modtool's message (and exit status: 2 is its usage text, a command it does not know) -> Problem (kind
    "other" when it is not one of the known ones)."""
    msg = message.strip()
    if code == 2:
        return Problem("tool", "This b3modtool is too old for the add-on", "it does not know the command",
                       "use the b3modtool of the same release as the add-on")
    if msg.startswith("car import: "):
        msg = msg[len("car import: "):]
    if msg.endswith(" (nothing written)"):
        msg = msg[: -len(" (nothing written)")]
    objs = []
    m = _OBJECT.match(msg)
    if m and m.group(1).startswith(("LOD", "X64", "X68")):
        objs.append(m.group(1))
    q = re.match(r'^"([^"]+)" ', msg)
    if q:
        objs.append(q.group(1))
    for kind, words, title, hint in _RULES:
        if words in msg:
            return Problem(kind, title, msg, hint, objs)
    return Problem("other", "b3modtool refused", msg, "", objs)
