#!/usr/bin/env python3
"""A stand-in for b3modtool in the add-on's unit tests (tools/blender/test_b3_car_tools.py): the commands the add-on
runs, with b3modtool's output format and files, and no game data. FAKE_B3_REFUSE=<message> makes every car command
refuse with that message (as b3modtool does: "b3modtool: <message>" on stderr, exit status 1). Each call is appended
to $FAKE_B3_LOG when set."""
import json
import os
import sys

SIDECAR = {
    "format": "b3modtool car export",
    "format_version": 1,
    "source": {"path": "PVEH/COMP/CAR1.BGV", "size": 436224, "fnv1a64": "e13c332ebf3476b0"},
    "name": "Compact Type 1",
    "texture": {"files": ["COMP_CAR1_paint0.png", "COMP_CAR1_paint1.png"]},
}
LIST = """\
  0  PVEH/COMP/CAR1.BGV   Compact      Compact Type 1
  1  PVEH/COMP/CAR2.BGV   Compact      Compact Type 2
 53  PVEH/HSPC/CAR23.BGV  Special      Classic Hotrod
 54  PVEH/HEVY/CAR36.BGV  Special      Fire Truck
traffic and other vehicles (export them by path):
     PVEH/COMP/CAR11.BTV
     PVEH/TSPC/CAR6.BTV
4 player cars, 2 traffic vehicles
"""
SUMMARY = ("PVEH/COMP/CAR1.BGV: 114 meshes (89 unchanged, 25 edited in place, 0 with new triangles, 0 not in the model: "
           "kept), 0 packets moved, 1 paints changed; 436224 -> 436224 bytes")


def fail(msg, code=1):
    sys.stdout.flush()
    sys.stderr.write("b3modtool: %s\n" % msg)
    sys.exit(code)


def main(a):
    if os.environ.get("FAKE_B3_LOG"):
        with open(os.environ["FAKE_B3_LOG"], "a", encoding="utf-8") as f:
            f.write(json.dumps(a) + "\n")
    opts = {}
    pos = []
    i = 0
    while i < len(a):
        if a[i] in ("--sidecar", "--game") and i + 1 < len(a):
            opts[a[i]] = a[i + 1]
            i += 2
            continue
        pos.append(a[i])
        i += 1
    if pos[:1] == ["car"] and os.environ.get("FAKE_B3_REFUSE"):
        fail(os.environ["FAKE_B3_REFUSE"])
    if pos[:2] == ["car", "list"] and len(pos) == 3:
        if not os.path.isdir(pos[2]):
            fail(pos[2] + ": not a disc folder or image")
        sys.stdout.write(LIST)
        return
    if pos[:2] == ["car", "export"] and len(pos) == 5:
        out = pos[4]
        os.makedirs(out, exist_ok=True)
        for n, t in (("COMP_CAR1.gltf", "{}"), ("COMP_CAR1.bin", ""), ("COMP_CAR1_paint0.png", ""),
                     ("COMP_CAR1_paint1.png", ""), ("COMP_CAR1.b3car.json", json.dumps(SIDECAR, indent=1))):
            with open(os.path.join(out, n), "w", encoding="utf-8") as f:
                f.write(t)
        print("PVEH/COMP/CAR1.BGV   Compact Type 1         11 files, 114 meshes, 310 nodes, 11824 vertices, 7320 "
              "triangles, 8 paints; bounds (-1.016 -0.314 -2.087) .. (1.016 1.122 2.064); glTF check ok")
        print("  note: damage: the game deforms the car at run time")
        print("car export: 1 of 1 cars written into %s, 1 glTF checked valid" % out)
        return
    if pos[:2] == ["car", "check"] and len(pos) == 4:
        if not os.path.exists(opts.get("--sidecar", "")):
            fail("cannot read the sidecar " + opts.get("--sidecar", ""))
        print("  note: 25 object(s) moved / turned / scaled (LOD3_L0_e00 .. LOD3_A5_e02): applied to their vertices")
        print("car check: " + SUMMARY + "; ok, nothing written")
        return
    if pos[:2] == ["car", "import"] and len(pos) == 5:
        out = pos[4]
        if out.upper().endswith((".BGV", ".BTV")):
            with open(out, "wb") as f:
                f.write(b"\0" * 16)
            print("car import: " + SUMMARY + " -> " + out)
            return
        pp = os.path.join(out, "patches", "PVEH", "COMP", "CAR1.BGV.b3patch")
        os.makedirs(os.path.dirname(pp), exist_ok=True)
        with open(pp, "wb") as f:
            f.write(b"B3PATCH")
        if not os.path.exists(os.path.join(out, "mod.ini")):
            with open(os.path.join(out, "mod.ini"), "w", encoding="utf-8") as f:
                f.write("[mod]\nid = x\n")
            print("  note: %s written (id x): change the name, author and description before you share the mod"
                  % os.path.join(out, "mod.ini"))
        print("car import: " + SUMMARY + " -> " + pp)
        return
    if pos[:1] == ["verify"] and len(pos) == 2:
        print("  note     no README: say what the mod changes")
        print("verify: OK %s: x 1.0, 2 files (0 disc paths, 1 patches), 0 errors, 0 warnings" % pos[1])
        return
    if pos[:1] == ["pack"] and len(pos) == 3:
        with open(pos[2], "wb") as f:
            f.write(b"PK")
        print("pack: %s (2 files)" % pos[2])
        return
    sys.stderr.write("b3modtool: the Burnout 3 port's mod asset tool (docs/modding/b3modtool.md)\n  usage ...\n")
    sys.exit(2)


if __name__ == "__main__":
    main(sys.argv[1:])
