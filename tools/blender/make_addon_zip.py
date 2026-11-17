#!/usr/bin/env python3
"""The Blender add-on as a zip that Blender installs (`make blender-addon`; the release packages carry it):

    tools/blender/make_addon_zip.py [out.zip]          default build/b3_car_tools.zip

One folder, b3_car_tools/, with the add-on's .py files and blender_manifest.toml: Blender 4.2+ installs it as an
extension (Edit > Preferences > Get Extensions > Install from Disk) or as a legacy add-on (Add-ons > Install from
Disk); bl_info in __init__.py serves the latter. No caches, fixed dates: the same sources give the same zip."""
import os
import sys
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "b3_car_tools")


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.dirname(HERE)), "build", "b3_car_tools.zip")
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    names = sorted(f for f in os.listdir(SRC) if f.endswith((".py", ".toml")))
    if "__init__.py" not in names or "blender_manifest.toml" not in names:
        sys.exit("make_addon_zip: %s is not the add-on" % SRC)
    tmp = out + ".tmp"
    with zipfile.ZipFile(tmp, "w", zipfile.ZIP_DEFLATED) as z:
        for n in names:
            with open(os.path.join(SRC, n), "rb") as f:
                data = f.read()
            info = zipfile.ZipInfo("b3_car_tools/" + n, date_time=(2026, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o644 << 16
            z.writestr(info, data)
    os.replace(tmp, out)
    print("make_addon_zip: %s (%d files)" % (out, len(names)))


if __name__ == "__main__":
    main()
