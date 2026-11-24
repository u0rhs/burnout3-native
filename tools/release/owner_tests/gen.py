#!/usr/bin/env python3
"""gen.py -- ownertests: scenarios.json for run-all-tests.ps1 from gen.sh's race_check captures.

  gen.py <capture dir> <scenarios.json>

A capture (one per race_check base scenario) holds native_play's arguments (args.txt, one a line), its environment
(env.txt; env.outer: the calling shell's, left out) and race_check's pc_settings.ini if it wrote one. Each run below
takes a base's --keys and environment and changes only what the window needs: no --offscreen / --no-pace / shots
folder, paced from `pace_from`, its own end, its own settings and environment on top. Python standard library only.
"""
import json
import os
import subprocess
import sys

# The runs (the owner's list, 2026-11-23). ini: pc_settings.ini rows on top of the defaults (the run's own file; the
# owner's is never read). env: on top of the base's. shots: polls (VBlank / 2) for B3_WINDOW_SHOTS. expect: log lines
# (regular expressions, each must occur; "N*re": at least N times) for the run's PASS, on top of the common ones.
# racetimefix: the night / rain runs must really be at night / in the rain (the shots' lines: offscreen "shot poll N: drawn
# at night (...)", in the window "window shot at tick N (B3_WINDOW_SHOTS), drawn at night")
NIGHT, RAIN = "drawn at night", "drawn in the rain"   # (at least one race shot each)
REFLECT = ["car_reflections = 2", "specular = 1", "reflect_distance = 1", "reflect_resolution = 1", "reflect_update = 0"]
RUNS = [
    dict(id="1", name="day-vulkan", title="Day race, Vulkan", base="race", ini=["graphics_api = 1"]),
    dict(id="2", name="day-d3d12", title="Day race, Direct3D 12", base="race", ini=["graphics_api = 2"], windows_only=True),
    dict(id="3", name="day-vsync", title="Day race, VSYNC on (against 1)", base="race", ini=["graphics_api = 1", "vsync = 1"]),
    dict(id="4a", name="night-depth-off", title="Night race, scene depth off (the pre-pass)", base="race",
         env={"B3_NIGHT": "1", "B3_SCENE_DEPTH": "0"}, expect=[NIGHT]),
    dict(id="4b", name="night-depth-on", title="Night race, scene depth on", base="race", env={"B3_NIGHT": "1", "B3_SCENE_DEPTH": "1"},
         expect=[NIGHT]),
    dict(id="5", name="rain", title="Rain race (day)", base="race", env={"B3_WEATHER": "rain"}, expect=[RAIN]),
    dict(id="6", name="night-rain", title="Night rain race", base="race", env={"B3_NIGHT": "1", "B3_WEATHER": "rain"},
         expect=[NIGHT, RAIN]),
    dict(id="7a", name="reflect-track", title="ULTRA reflections: Track (env timing)", base="race",
         ini=REFLECT + ["reflect_detail = 0"], env={"B3_ENV_TIME": "1"}, expect=["env time: [0-9.]+ ms"]),
    dict(id="7b", name="reflect-props-cars", title="ULTRA reflections: Track, props and cars (env timing)", base="race",
         ini=REFLECT + ["reflect_detail = 2"], env={"B3_ENV_TIME": "1"}, expect=["env time: [0-9.]+ ms"]),
    dict(id="8", name="replay", title="RACE REPLAY: finish, REPLAY, the cameras", base="replaycams", end=26600, pace_from=23400,
         # (START AT's race is shorter -- EU/M1_V1, 2 laps, against race_check's US/C2_V2, 3 laps -- so its replay ends
         # ~600 VBlanks sooner: the scripted pad's D-pad up moved from 25700 into it)
         move={"25700:1035:1": "24920:1035:1", "25720:1035:0": "24940:1035:0"},
         shots=[14100, 14700, 15300],
         expect=["replay_view: the replay starts", r"\[replaycams\] poll [0-9]*: the replay plays",
                 r"\[replaycams\] camera next \(test pad\)"]),
    dict(id="9", name="eight-cars", title="8-car race (RIVALS 7)", base="morerivals", shots=[10400, 11000],
         expect=["2*more_rivals: AI car [0-9]*: car id", r"more_rivals: grid check.*: PASS \(8 cars\)"]),
    dict(id="10", name="crash-results", title="CRASH: results confirmed with a controller's A", base="crashresults",
         end=30400, pace_from=26500, shots=[13600, 14200], pad_a_from=27400, env={"B3NATIVE_TEST_PAD": "xbox"},
         expect=[r"test pad: input 0 down \(menus\): the game gets CROSS"]),
]
RACE_END, RACE_PACE, RACE_SHOTS = 22400, 19800, [10500, 11100]   # ~32 s of driving, paced from SELECT CAR
# (lines a window run prints too: native_play's own, not the offscreen feed's summary)
COMMON_EXPECT = [r"native_play: .*VBlank limit reached after [0-9]+ virtual VBlanks", r"native_play: VBlank [0-9]+: keys: driving"]
KEEP_ENV = ("B3", "SDL")
DROP_ENV = {"B3NATIVE_SAVES_ROOT", "B3NATIVE_MAX_VBLANKS", "B3_GAME_DIR", "B3_CUSTOM_CARS"}
# runner2: START AT main menu (B3NATIVE_BOOT_TO=2, tools/native/boot_boost.h: the films ended as they start, the title's
# Start pressed by the port, profile 1 loaded): race_check's keys before its main menu's first key (FRONT_FROM: the title's
# Start, LOAD PROFILE, PROFILE 1) are dropped and the rest moved SHIFT VBlanks earlier, so that first key comes at
# FRONT_FROM - SHIFT, ~700 VBlanks after the main menu is up (by VBlank 1000 offscreen); everything after it keeps
# race_check's own spacing, and the end, --pace-from and the shots move with it
FRONT_FROM, SHIFT = 17900, 16200
PAD_A = 1024   # nativepad.h kTestPadCode + SDL_GAMEPAD_BUTTON_SOUTH
KEY_CROSS = 29  # Z: Cross (docs/mac.md §5)


def read_env(path):
    env = {}
    for line in open(path, encoding="utf-8", errors="replace"):
        if "=" in line:
            k, v = line.rstrip("\n").split("=", 1)
            env[k] = v
    return env


def capture(d, outer):
    args = open(os.path.join(d, "args.txt"), encoding="utf-8").read().split("\n")
    out = {"keys": "", "keys2": "", "render": [], "end": None, "env": {}, "ini": []}
    i = 0
    while i < len(args):
        a = args[i]
        nxt = args[i + 1] if i + 1 < len(args) else ""
        if a in ("--keys", "--keys2"):
            out[a[2:]] = nxt; i += 2; continue
        if a == "--render-args":
            r = nxt.split()
            j = 0
            while j < len(r):
                if r[j] in ("--offscreen-live", "--settings", "--shots"):
                    j += 2; continue   # (a folder of race_check's, its ini (read below), its shots)
                out["render"].append(r[j]); j += 1
            i += 2; continue
        if a in ("--game", "--record-pad", "--pace-from"):
            i += 2; continue
        if a in ("--offscreen", "--no-pace", "--strict", ""):
            i += 1; continue
        raise SystemExit("gen.py: %s: an argument this generator does not know: %s" % (d, a))
    env = read_env(os.path.join(d, "env.txt"))
    out["end"] = int(env.get("B3NATIVE_MAX_VBLANKS", "0")) or None
    out["env"] = {k: v for k, v in sorted(env.items())
                  if k.startswith(KEEP_ENV) and k not in DROP_ENV and outer.get(k) != v}
    ini = os.path.join(d, "pc_settings.ini")
    if os.path.isfile(ini):
        out["ini"] = [l.strip() for l in open(ini, encoding="utf-8") if l.strip()]
    if out["render"]:
        raise SystemExit("gen.py: %s: renderer arguments this generator does not carry: %s" % (d, out["render"]))
    return out


def pad_a(keys, start):
    """the keyboard's Cross taps from VBlank `start` on as the scripted pad's A (padskip: a controller's A)"""
    out = []
    for e in keys.split(","):
        v, c, s = e.split(":")
        if int(v) >= start and int(c) == KEY_CROSS:
            c = str(PAD_A)
        out.append("%s:%s:%s" % (v, c, s))
    return ",".join(out)


def fast_front(keys):
    out = []
    for e in keys.split(","):
        v, c, st = e.split(":")
        if int(v) >= FRONT_FROM:
            out.append("%d:%s:%s" % (int(v) - SHIFT, c, st))
    return ",".join(out)


def main():
    cap_dir, dest = sys.argv[1], sys.argv[2]
    outer = read_env(os.path.join(cap_dir, "env.outer"))
    bases = {b: capture(os.path.join(cap_dir, b), outer) for b in ("race", "replaycams", "morerivals", "crashresults")}
    runs = []
    for r in RUNS:
        b = bases[r["base"]]
        keys = b["keys"]
        if "pad_a_from" in r:
            keys = pad_a(keys, r["pad_a_from"])
        for a, b2 in r.get("move", {}).items():
            if a not in keys.split(","):
                raise SystemExit("gen.py: run %s: key %s not in race_check's script" % (r["id"], a))
            keys = ",".join(b2 if e == a else e for e in keys.split(","))
        keys = ",".join(sorted(keys.split(","), key=lambda e: int(e.split(":")[0])))
        keys = fast_front(keys)
        env = dict(b["env"])
        env["B3NATIVE_BOOT_TO"] = "2"
        env.update(r.get("env", {}))
        runs.append({
            "id": r["id"], "name": r["name"], "title": r["title"], "base": r["base"],
            "keys": keys, "keys2": b["keys2"], "env": env, "ini": b["ini"] + ["fps_view = 3"] + r.get("ini", []),   # (the Detailed FPS plate: to watch)
            "end_vblanks": r.get("end", RACE_END) - SHIFT, "pace_from": r.get("pace_from", RACE_PACE) - SHIFT,
            "shots": [p - SHIFT // 2 for p in r.get("shots", RACE_SHOTS)], "expect": COMMON_EXPECT + r.get("expect", []),
            "windows_only": r.get("windows_only", False),
        })
    try:
        commit = subprocess.run(["git", "rev-parse", "--short=8", "HEAD"], capture_output=True, text=True).stdout.strip()
    except OSError:
        commit = ""
    json.dump({"format": 1, "made_from": commit, "runs": runs}, open(dest, "w"), indent=1)
    print("gen.py: %d runs written to %s (from race_check at %s)" % (len(runs), dest, commit or "?"))


if __name__ == "__main__":
    main()
