#!/usr/bin/env python3
"""Unit tests of the Blender add-on's plain-Python modules (tools/blender/b3_car_tools: tool, errors, props,
gltf_settings, workflow), with the system python3 and no Blender:

    python3 -I tools/blender/test_b3_car_tools.py          (part of `make test`)

b3modtool is played by tools/blender/fake_b3modtool.py. When build/b3modtool and $B3_GAME_DIR (your disc) are
there, a few tests also run the real one on the disc: the car list, an export, `car check` of it (every mesh
unchanged) and a mod made from it (verify OK); their files go to build/blender_test and are deleted after.
The bpy layer (ops.py) and Blender's glTF importer / exporter are checked by tools/modding/blender_check.sh."""
import json
import os
import shutil
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)

from b3_car_tools import errors, gltf_settings, props, tool, workflow  # noqa: E402

FAKE = [sys.executable, "-I", os.path.join(HERE, "fake_b3modtool.py")]
SIDE = json.dumps({
    "format": "b3modtool car export", "format_version": 1,
    "source": {"path": "PVEH/COMP/CAR1.BGV", "size": 436224, "fnv1a64": "e13c332ebf3476b0"},
    "name": "Compact Type 1", "texture": {"files": ["a.png", "b.png", "c.png"]}})


class TestTool(unittest.TestCase):
    def test_find_order(self):
        files = {"/rel/b3modtool", "/rel/build/b3modtool", "/env/b3modtool", "/path/b3modtool", "/repo/build/b3modtool"}
        isfile = files.__contains__
        which = lambda n: "/path/b3modtool" if n == "b3modtool" else None   # noqa: E731
        old = tool.EXE
        tool.EXE = "b3modtool"
        try:
            # the preference: a file
            self.assertEqual(tool.find("/rel/b3modtool", "", {}, which, isfile), "/rel/b3modtool")
            # $B3MODTOOL, then a checkout around the add-on, then PATH
            self.assertEqual(tool.find("", "", {"B3MODTOOL": "/env/b3modtool"}, which, isfile), "/env/b3modtool")
            self.assertEqual(tool.find("", "/repo/tools/blender/b3_car_tools", {}, which, isfile), "/repo/build/b3modtool")
            self.assertEqual(tool.find("", "", {}, which, isfile), "/path/b3modtool")
            with self.assertRaises(tool.ToolMissing):
                tool.find("", "", {}, lambda n: None, isfile)
        finally:
            tool.EXE = old
        # a folder: the release folder's program
        with tempfile.TemporaryDirectory() as d:
            p = os.path.join(d, tool.EXE)
            open(p, "w").close()
            self.assertEqual(tool.find(d, "", {}, lambda n: None), p)

    def test_commands(self):
        t = ["b3modtool"]
        self.assertEqual(tool.cmd_car_list(t, "D"), ["b3modtool", "car", "list", "D"])
        self.assertEqual(tool.cmd_car_export(t, "D", "PVEH/COMP/CAR1.BGV", "out dir"),
                         ["b3modtool", "car", "export", "D", "PVEH/COMP/CAR1.BGV", "out dir"])
        self.assertEqual(tool.cmd_car_check(t, "D", "m.glb", "s.b3car.json"),
                         ["b3modtool", "car", "check", "D", "m.glb", "--sidecar", "s.b3car.json"])
        self.assertEqual(tool.cmd_car_import(t, "D", "m.glb", "s.json", "mods/x"),
                         ["b3modtool", "car", "import", "D", "m.glb", "mods/x", "--sidecar", "s.json"])
        self.assertEqual(tool.cmd_pack(t, "mods/x", "x.b3mod", "D"), ["b3modtool", "pack", "mods/x", "x.b3mod", "--game", "D"])
        self.assertEqual(tool.cmd_verify(t, "mods/x"), ["b3modtool", "verify", "mods/x"])

    def test_parse_car_list(self):
        text = ("  0  PVEH/COMP/CAR1.BGV   Compact      Compact Type 1\n"
                " 54  PVEH/HEVY/CAR36.BGV  Special      Fire Truck\n"
                " 66  PVEH/HEVY/CAR10.BGV  Heavyweight  Trash Truck\n"
                "traffic and other vehicles (export them by path):\n"
                "     PVEH/COMP/CAR11.BTV\n"
                "note: something\n"
                "3 player cars, 1 traffic vehicles\n")
        cars = tool.parse_car_list(text)
        self.assertEqual([(c.index, c.key, c.kind, c.name) for c in cars],
                         [(0, "PVEH/COMP/CAR1.BGV", "Compact", "Compact Type 1"),
                          (54, "PVEH/HEVY/CAR36.BGV", "Special", "Fire Truck"),
                          (66, "PVEH/HEVY/CAR10.BGV", "Heavyweight", "Trash Truck"),
                          (-1, "PVEH/COMP/CAR11.BTV", "Traffic", "")])
        self.assertEqual(cars[3].label, "PVEH/COMP/CAR11.BTV")
        for t, want in (("fire truck", 1), ("54", 1), ("PVEH/HEVY/CAR36.BGV", 1), ("hevy/car36", 1),
                        ("COMP\\CAR11.BTV", 3), ("0", 0), ("nope", None), ("", None)):
            c = tool.resolve_car(cars, t)
            self.assertIs(c, None if want is None else cars[want], t)

    def test_parse_import(self):
        out = ("  note: 25 object(s) moved / turned / scaled (LOD3_L0_e00 .. LOD3_A5_e02): applied to their vertices\n"
               "  note: mods/big/mod.ini written (id big): change the name\n"
               "car import: PVEH/COMP/CAR1.BGV: 114 meshes (89 unchanged, 25 edited in place, 0 with new triangles, "
               "0 not in the model: kept), 0 packets moved, 1 paints changed; 436224 -> 436224 bytes -> "
               "mods/big/patches/PVEH/COMP/CAR1.BGV.b3patch\n")
        r = tool.parse_import(out)
        self.assertEqual(len(r["notes"]), 2)
        self.assertEqual((r["command"], r["car"], r["meshes"], r["unchanged"], r["edited"], r["restripped"], r["kept"],
                          r["moved"], r["paints"], r["requantised"], r["size_before"], r["size_after"]),
                         ("import", "PVEH/COMP/CAR1.BGV", 114, 89, 25, 0, 0, 0, 1, False, 436224, 436224))
        self.assertEqual(r["written"], "mods/big/patches/PVEH/COMP/CAR1.BGV.b3patch")
        self.assertIn("same size", tool.summary_text(r))
        c = tool.parse_import("car check: PVEH/COMP/CAR1.BGV: 114 meshes (100 unchanged, 0 edited in place, 14 with new "
                              "triangles, 0 not in the model: kept), 3 packets moved, 2 paints changed (re-quantised "
                              "together); 436224 -> 440000 bytes; ok, nothing written\n")
        self.assertEqual((c["command"], c["restripped"], c["moved"], c["requantised"], c["written"]), ("check", 14, 3, True, ""))
        self.assertIn("+3776", tool.summary_text(c))
        self.assertEqual(tool.summary_text(tool.parse_import("")), "")

    def test_refusal_and_dirs(self):
        r = tool.Result(1, "some output\n", "b3modtool: car import: \"Cube\" is not one of the car's meshes (nothing written)\n")
        self.assertEqual(r.refusal(), "car import: \"Cube\" is not one of the car's meshes (nothing written)")
        self.assertEqual(tool.Result(3, "", "").refusal(), "b3modtool stopped (exit status 3) without a message")
        taken = {"w/fire-truck", "w/fire-truck_2"}
        self.assertEqual(tool.fresh_dir("w/fire-truck", taken.__contains__), "w/fire-truck_3")
        self.assertEqual(tool.fresh_dir("w/hotrod", taken.__contains__), "w/hotrod")
        self.assertEqual(tool.safe_name("Euro-Circuit Racer"), "euro-circuit-racer")
        self.assertEqual(tool.safe_name("PVEH/COMP/CAR11.BTV"), "pveh-comp-car11-btv")
        self.assertEqual(tool.export_model("d", "d/COMP_CAR1.b3car.json"), os.path.join("d", "COMP_CAR1.gltf"))


# b3modtool's refusals, as it words them (src/b3modtool/carimport.cpp, txd.cpp, tools/modding/b3modtool.cpp)
MESSAGES = [
    ("size", "car import: the new file would be 470000 bytes, over the limit of 466944 (memory block 1, which a player "
             "car is loaded into); the game cannot load it. Use fewer vertices (90000 vertices in the rewritten meshes "
             "now), or leave out LODs you did not mean to change (nothing written)"),
    ("uv", "car import: LOD3_L0_e02: 12 texture coordinates outside 0 .. 16 (the game stores them as 0 .. 65535 / "
           "4096): keep the UVs on the texture (nothing written)"),
    ("foreign", "car import: \"Cube\" is not one of the car's meshes (names like LOD3_L0_e00): the import changes the "
                "meshes the file has and cannot add objects; join new parts into one of them (nothing written)"),
    ("foreign", "car import: \"LOD3_L0_e40\" names LOD3_L0_e40, which this car's file does not have (nothing written)"),
    ("twice", "car import: two different meshes stand for LOD3_B0_e00 (joined copies of a wheel? keep one mesh per "
              "entry) (nothing written)"),
    ("position", "car import: LOD3_A2_e00: a vertex position outside +-1000 m (or not a number) (nothing written)"),
    ("triangles", "car import: LOD3_L0_e00: a primitive that is not triangles (mode 1) (nothing written)"),
    ("uvmap", "car import: LOD2_L0_e01: no texture coordinates (TEXCOORD_0): every vertex needs its place in the car's "
              "texture (nothing written)"),
    ("paint", "car import: compact1 paint 3 is 256x128; the picture given is 512x256 (the size cannot change) "
              "(nothing written)"),
    ("zone", "car import: LOD3_L0_e00: influence 0x00000124 is not one the file uses (zone 9, the file's zones are "
             "0..7) (nothing written)"),
    ("other_file", "car import: the export was made from another PVEH/COMP/CAR1.BGV than this one (its size and hash "
                   "differ): export the car again from this disc (nothing written)"),
    ("sidecar", "no .b3car.json beside m.glb (the export's sidecar: keep it in the export folder, or name it with --sidecar)"),
    ("sidecar", "cannot read the sidecar /x/COMP_CAR1.b3car.json"),
    ("disc", "/mnt/nothing: not a disc folder or image"),
    ("disc", "/games/b3 has no PVEH/COMP/CAR1.BGV"),
    ("mod", "mods/x/data/PVEH/COMP/CAR1.BGV is there too: a mod has a file or its patch, not both"),
    ("writer", "car import: LOD3_L0_e00: the written packets do not read back as meant (a writer error; nothing written)"),
    ("other", "something new"),
]


class TestErrors(unittest.TestCase):
    def test_classify(self):
        for kind, msg in MESSAGES:
            p = errors.classify(msg)
            self.assertEqual(p.kind, kind, msg)
            self.assertFalse(p.detail.startswith("car import: "))
            self.assertFalse(p.detail.endswith("(nothing written)"))
            self.assertIn(p.title, p.report())
        self.assertEqual(errors.classify(MESSAGES[2][1]).objects, ["Cube"])
        self.assertEqual(errors.classify(MESSAGES[1][1]).objects, ["LOD3_L0_e02"])
        self.assertEqual(errors.classify("b3modtool: the Burnout 3 port's mod asset tool", 2).kind, "tool")

    def test_rules_match_b3modtool(self):
        """Each rule's words are in b3modtool's sources: a reworded message there fails here, not in Blender."""
        src = ""
        for p in ("src/b3modtool/carimport.cpp", "src/b3modtool/txd.cpp", "tools/modding/b3modtool.cpp"):
            with open(os.path.join(REPO, p), encoding="utf-8") as f:
                src += f.read()
        for kind, words, _title, _hint in errors._RULES:
            if words == " has no PVEH/":   # built as d + " has no " + key
                self.assertIn('" has no "', src)
                continue
            self.assertIn(words, src, "%s: %r" % (kind, words))


class TestProps(unittest.TestCase):
    def test_round_trip(self):
        p = props.car_props(SIDE, "/w/compact-type-1", "COMP_CAR1.b3car.json", "COMP_CAR1.gltf", "/games/b3")
        self.assertEqual((p["name"], p["source_path"], p["source_size"], p["source_fnv1a64"], p["paints"]),
                         ("Compact Type 1", "PVEH/COMP/CAR1.BGV", 436224, "e13c332ebf3476b0", 3))
        for v in p.values():   # Blender ID properties: strings and 32-bit ints only
            self.assertTrue(isinstance(v, str) or (isinstance(v, int) and -2**31 <= v < 2**31))
        props.check(p)
        self.assertEqual(props.sidecar_path(p), os.path.join(os.path.abspath("/w/compact-type-1"), "COMP_CAR1.b3car.json"))
        self.assertTrue(props.same_source(p, SIDE))
        other = json.loads(SIDE)
        other["source"]["fnv1a64"] = "0000000000000000"
        self.assertFalse(props.same_source(p, json.dumps(other)))
        # the sidecar written back from the .blend when the export folder lost it
        with tempfile.TemporaryDirectory() as d:
            p2 = props.car_props(SIDE, os.path.join(d, "gone"), "COMP_CAR1.b3car.json", "COMP_CAR1.gltf", "/games/b3")
            path, restored = props.ensure_sidecar(p2)
            self.assertTrue(restored)
            with open(path, encoding="utf-8") as f:
                self.assertEqual(f.read(), SIDE)
            self.assertEqual(props.ensure_sidecar(p2), (path, False))

    def test_refusals(self):
        with self.assertRaises(props.PropsError):
            props.car_props("{not json", "/w", "s", "m", "d")
        with self.assertRaises(props.PropsError):
            props.car_props(json.dumps({"format": "b3modtool track export"}), "/w", "s", "m", "d")
        with self.assertRaises(props.PropsError):
            props.check({})
        p = props.car_props(SIDE, "/w", "s.b3car.json", "m", "d")
        del p["sidecar_json"]
        with self.assertRaises(props.PropsError):
            props.check(p)
        self.assertTrue(props.is_root_extras({"file": "PVEH/COMP/CAR1.BGV", "stem": "COMP_CAR1", "sidecar": "x"}))
        self.assertFalse(props.is_root_extras({"lod": 3}))
        self.assertFalse(props.is_root_extras(None))


class TestGltfSettings(unittest.TestCase):
    # the import / export operators' option names in Blender 4.2 LTS (a subset: the ones the add-on sets and others)
    IMP = {"filepath", "merge_vertices", "import_shading", "import_pack_images", "guess_original_bind_pose",
           "bone_heuristic", "import_select_created_objects", "import_scene_extras", "export_import_convert_lighting_mode"}
    EXP = {"filepath", "check_existing", "export_format", "use_selection", "use_visible", "use_renderable",
           "use_active_collection", "use_active_collection_with_nested", "use_active_scene", "export_extras",
           "export_yup", "export_apply", "export_texcoords", "export_normals", "export_attributes", "export_tangents",
           "export_materials", "export_image_format", "export_draco_mesh_compression_enable", "export_shared_accessors",
           "export_loose_edges", "export_loose_points", "export_gpu_instances", "export_animations", "export_skins",
           "export_morph", "export_cameras", "export_lights", "export_hierarchy_flatten_objs",
           "export_hierarchy_full_collections", "will_save_settings", "export_vertex_color"}

    def test_import(self):
        kw = gltf_settings.import_kwargs(self.IMP, "/w/c/COMP_CAR1.gltf")
        self.assertEqual(kw["filepath"], "/w/c/COMP_CAR1.gltf")
        self.assertIs(kw["merge_vertices"], False)
        self.assertEqual(kw["import_shading"], "NORMALS")
        self.assertIs(kw["import_pack_images"], False)
        self.assertTrue(set(kw) <= self.IMP)
        self.assertEqual(set(gltf_settings.import_kwargs({"filepath"}, "x")), {"filepath"})

    def test_export(self):
        kw, warn = gltf_settings.export_kwargs(self.EXP, "/w/c/COMP_CAR1_blender.glb", apply_modifiers=False)
        self.assertEqual(warn, [])
        self.assertTrue(set(kw) <= self.EXP)
        for k, v in (("export_format", "GLB"), ("use_active_collection", True), ("use_selection", False),
                     ("use_visible", False), ("export_attributes", True), ("export_extras", True), ("export_yup", True),
                     ("export_draco_mesh_compression_enable", False), ("export_materials", "PLACEHOLDER"),
                     ("export_shared_accessors", False), ("export_loose_edges", False), ("export_apply", False)):
            self.assertEqual(kw[k], v, k)
        self.assertNotIn("export_vertex_color", kw)   # left at Blender's default
        self.assertTrue(gltf_settings.export_kwargs(self.EXP, "x")[0]["export_apply"])
        # a Blender without some options: left out, with a warning where it matters, selection instead of the collection
        kw, warn = gltf_settings.export_kwargs({"filepath", "use_selection", "export_format"}, "x")
        self.assertEqual(kw, {"filepath": "x", "use_selection": True, "export_format": "GLB"})
        self.assertEqual(len(warn), 3)
        self.assertEqual(gltf_settings.model_name("COMP_CAR1"), "COMP_CAR1_blender.glb")


class TestWorkflowFake(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.d = self.tmp.name
        self.disc = os.path.join(self.d, "disc")
        os.makedirs(self.disc)
        os.environ.pop("FAKE_B3_REFUSE", None)

    def tearDown(self):
        os.environ.pop("FAKE_B3_REFUSE", None)
        self.tmp.cleanup()

    def test_list_export_check_mod(self):
        cars = workflow.list_cars(FAKE, self.disc)
        self.assertEqual(len(cars), 6)
        car = tool.resolve_car(cars, "Compact Type 1")
        work = os.path.join(self.d, "work")
        ex = workflow.export_car(FAKE, self.disc, car, work)
        self.assertEqual(ex.export_dir, os.path.join(work, "compact-type-1"))
        self.assertEqual(ex.sidecar, os.path.join(ex.export_dir, "COMP_CAR1.b3car.json"))
        self.assertEqual(ex.model, os.path.join(ex.export_dir, "COMP_CAR1.gltf"))
        self.assertEqual(ex.notes, ["damage: the game deforms the car at run time"])
        self.assertTrue(ex.summary.startswith("PVEH/COMP/CAR1.BGV"))
        # a second import of the same car: a new folder (the first one's paint PNGs are kept)
        self.assertEqual(workflow.export_car(FAKE, self.disc, car, work).export_dir, ex.export_dir + "_2")
        with open(ex.sidecar, encoding="utf-8") as f:
            p = props.car_props(f.read(), ex.export_dir, os.path.basename(ex.sidecar), os.path.basename(ex.model), self.disc)
        model = os.path.join(ex.export_dir, gltf_settings.model_name("COMP_CAR1"))
        r = workflow.check(FAKE, self.disc, model, props.sidecar_path(p))
        self.assertEqual((r["command"], r["edited"], len(r["notes"])), ("check", 25, 1))
        # a mod folder, then a .b3mod (its folder beside it), then a car file
        mod = os.path.join(self.d, "mods", "compact-mod")
        r = workflow.make_mod(FAKE, self.disc, model, ex.sidecar, mod)
        self.assertTrue(os.path.isfile(os.path.join(mod, "patches", "PVEH", "COMP", "CAR1.BGV.b3patch")))
        self.assertEqual(r["mod_dir"], mod)
        self.assertTrue(r["verify"].startswith("verify: OK"))
        self.assertNotIn("b3mod", r)
        b3 = os.path.join(self.d, "mods", "compact.b3mod")
        r = workflow.make_mod(FAKE, self.disc, model, ex.sidecar, b3)
        self.assertEqual((r["b3mod"], r["mod_dir"]), (b3, b3[:-len(".b3mod")]))
        self.assertTrue(os.path.isfile(b3))
        bgv = os.path.join(self.d, "CAR1.BGV")
        r = workflow.make_mod(FAKE, self.disc, model, ex.sidecar, bgv)
        self.assertTrue(os.path.isfile(bgv) and "mod_dir" not in r)

    def test_refusals(self):
        with self.assertRaises(workflow.Failed) as e:
            workflow.list_cars(FAKE, os.path.join(self.d, "no-disc"))
        self.assertEqual(e.exception.problem.kind, "disc")
        car = tool.Car(0, "PVEH/COMP/CAR1.BGV", "Compact", "Compact Type 1")
        ex = workflow.export_car(FAKE, self.disc, car, self.d)
        for kind, msg in MESSAGES:
            if kind in ("other", "disc"):
                continue
            os.environ["FAKE_B3_REFUSE"] = msg
            with self.assertRaises(workflow.Failed) as e:
                workflow.make_mod(FAKE, self.disc, ex.model, ex.sidecar, os.path.join(self.d, "m"))
            self.assertEqual(e.exception.problem.kind, kind, msg)
            with self.assertRaises(workflow.Failed) as e:
                workflow.check(FAKE, self.disc, ex.model, ex.sidecar)
            self.assertEqual(e.exception.problem.kind, kind, msg)
        self.assertFalse(os.path.exists(os.path.join(self.d, "m")))
        # an older b3modtool without `car check`: its usage text, exit status 2
        old = [sys.executable, "-I", "-c", "import sys; sys.stderr.write('b3modtool: the mod asset tool\\n'); sys.exit(2)"]
        os.environ.pop("FAKE_B3_REFUSE", None)
        with self.assertRaises(workflow.Failed) as e:
            workflow.check(old, self.disc, ex.model, ex.sidecar)
        self.assertEqual(e.exception.problem.kind, "tool")
        with self.assertRaises(tool.ToolMissing):
            workflow.list_cars([os.path.join(self.d, "no-such-program")], self.disc)


class TestOpsWithStub(unittest.TestCase):
    """ops.py (the bpy layer) imported against a stand-in bpy module: its classes are made and register, and the
    car's root is found from an object's parents. Blender itself: tools/modding/blender_check.sh."""

    def test_import_and_register(self):
        import types
        reg = []

        class Base:
            pass

        def prop(**kw):
            return ("prop", kw)

        class Menu:
            def __init__(self):
                self.items = []

            def append(self, f):
                self.items.append(f)

            def remove(self, f):
                self.items.remove(f)

        bpy = types.ModuleType("bpy")
        bpy.types = types.SimpleNamespace(Operator=Base, Panel=Base, AddonPreferences=Base,
                                          TOPBAR_MT_file_import=Menu(), TOPBAR_MT_file_export=Menu())
        bpy.props = types.SimpleNamespace(BoolProperty=prop, EnumProperty=prop, StringProperty=prop)
        bpy.utils = types.SimpleNamespace(register_class=reg.append, unregister_class=reg.remove)
        bpy.path = types.SimpleNamespace(abspath=lambda p: p)
        saved = {k: sys.modules.get(k) for k in ("bpy", "bpy.props", "b3_car_tools.ops")}
        sys.modules["bpy"] = bpy
        sys.modules["bpy.props"] = bpy.props
        sys.modules.pop("b3_car_tools.ops", None)
        try:
            from b3_car_tools import ops
            ops.register()
            self.assertEqual(len(reg), len(ops.CLASSES))
            self.assertEqual(ops.B3CarToolsPreferences.bl_idname, "b3_car_tools")
            self.assertEqual({c.bl_idname for c in ops.CLASSES if hasattr(c, "bl_idname") and c.bl_idname.startswith("b3.")},
                             {"b3.import_car", "b3.check_car", "b3.export_mod", "b3.open_folder"})
            self.assertEqual(len(bpy.types.TOPBAR_MT_file_import.items), 1)
            # the root: from an object's parents, by the "b3_car" property
            root = {props.KEY: {"format": props.FORMAT}}

            class Ob(dict):
                def __init__(self, parent, d=()):
                    super().__init__(d)
                    self.parent = parent

            r = Ob(None, root)
            child = Ob(Ob(r))
            ctx = types.SimpleNamespace(active_object=child, view_layer=None)
            self.assertIs(ops.car_root(ctx), r)
            ops.unregister()
            self.assertEqual(reg, [])
        finally:
            for k, v in saved.items():
                if v is None:
                    sys.modules.pop(k, None)
                else:
                    sys.modules[k] = v


GAME = os.environ.get("B3_GAME_DIR", "")
REAL = os.path.join(REPO, "build", "b3modtool" + (".exe" if sys.platform.startswith("win") else ""))


@unittest.skipUnless(GAME and os.path.isdir(GAME) and os.path.isfile(REAL), "needs build/b3modtool and B3_GAME_DIR")
class TestWorkflowReal(unittest.TestCase):
    """The real b3modtool on your disc: the add-on's reading of its output (no Blender: the export's own glTF is the
    model, which `car import` gives back as the disc's bytes)."""

    def test_real(self):
        work = os.path.join(REPO, "build", "blender_test")
        shutil.rmtree(work, ignore_errors=True)
        try:
            cars = workflow.list_cars([REAL], GAME)
            self.assertEqual(sum(1 for c in cars if c.index >= 0), 67)
            self.assertEqual(tool.resolve_car(cars, "0").key, "PVEH/COMP/CAR1.BGV")
            ex = workflow.export_car([REAL], GAME, tool.resolve_car(cars, "Compact Type 1"), work)
            self.assertTrue(os.path.isfile(ex.model) and ex.notes)
            # every "_" attribute one float SCALAR (Blender's importer pairs them with their data in a set's order:
            # only attributes of one form come back right in every run)
            with open(ex.model, encoding="utf-8") as f:
                g = json.load(f)
            names = set()
            for m in g["meshes"]:
                for p in m["primitives"]:
                    for k, i in p["attributes"].items():
                        if k.startswith("_"):
                            names.add(k)
                            a = g["accessors"][i]
                            self.assertEqual((a["type"], a["componentType"]), ("SCALAR", 5126), k)
            self.assertEqual(names, {"_B3_NRM_X", "_B3_NRM_Y", "_B3_NRM_Z", "_B3_INFL_0", "_B3_INFL_1", "_B3_W"})
            r = workflow.check([REAL], GAME, ex.model, ex.sidecar)
            self.assertEqual((r["unchanged"], r["edited"], r["restripped"]), (r["meshes"], 0, 0))
            self.assertEqual(r["size_before"], r["size_after"])
            r = workflow.make_mod([REAL], GAME, ex.model, ex.sidecar, os.path.join(work, "mods", "same"))
            self.assertTrue(r["verify"].startswith("verify: OK"), r["verify"])
            self.assertTrue(os.path.isfile(r["written"]))
        finally:
            shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    unittest.main(verbosity=1)
