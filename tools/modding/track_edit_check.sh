#!/bin/bash
# track_edit_check.sh -- an edited route in the game (mod kit part 3c, the track import: docs/modding/b3modtool.md
# "track import"): a test mod made here with `b3modtool track export` / `track import` from the user's own disc -- the
# route the race check drives, every section's meshes (high detail and LOD) with the green taken out of their vertex
# colours in the exported glTF (COLOR_0 only, as a modelling tool's paint does it), imported with every mesh's strips
# rebuilt (--restrip: every packet of the route written anew, blocks grown and moved; the game must see the same
# triangles) as b3patches of STATIC.DAT / STREAMED.DAT -- loaded by native_play through --mods in an offscreen race_check
# run, against the same run without it.
#
#   tools/modding/track_edit_check.sh [native_play] [out dir]
#     native_play   default build/native_play
#     out dir       default build/track_edit_check (the export, the test mod, both runs' logs and shots, the difference
#                   masks; game data: delete it after)
#   Environment: B3_GAME_DIR (a folder of the disc's files), SAVES (as race_check.sh), MODTOOL (default
#   build/b3modtool), REF (a no-mod race_check out dir of the same build: that run is not repeated), EVIDENCE (a folder
#   the race shots of both runs and the masks are copied to), RESTRIP (default 1; 0: only the colour edit),
#   RENDER_EXTRA (passed on to race_check, both runs)
#
# PASS (last line "track_edit_check: PASS ...") when the import passed its own checks (the new files decode to what was
# meant), both runs PASS race_check (the same race: the collision and the AI paths are the disc's), the modded log names
# the set and the game read the route's files from the mod's patches, the renderer built the same set and drew the same
# route, the plain run has no mods, and each race shot changed with at least 90 % of the changed pixels less green
# (green down or equal: the sections' colours lost their green, nothing else changed colour).
set -u
cd "$(dirname "$0")/../.." || exit 1
NP=${1:-build/native_play}
OUT=${2:-build/track_edit_check}
MT=${MODTOOL:-build/b3modtool}
. tools/local_env.sh; b3_local_env .
GAME=${B3_GAME_DIR:-data/game}
RACE_SHOTS="10700 11500"
fail() { echo "track_edit_check: FAIL $*"; exit 1; }
[ -x "$NP" ] || fail "no $NP"
[ -x "$MT" ] || fail "no $MT (make modtool)"
[ -d "$GAME" ] || fail "B3_GAME_DIR ($GAME) is not a folder of the disc's files"
case $OUT in /*) ;; *) OUT=$PWD/$OUT ;; esac
mkdir -p "$OUT"
MODS_DIR=$OUT/mods
MOD=$MODS_DIR/proof-track-edit

run() {   # run <out dir> [MODS]
  local dir=$1
  env ${2:+MODS=$2 MODS_DIR=$MODS_DIR} tools/native/race_check.sh "$NP" "$dir" > "$dir.verdict.txt" 2>&1
  tail -1 "$dir.verdict.txt"
}
if [ -n "${REF:-}" ]; then R=$REF; V2="race_check: PASS (REF $REF, not checked here)"
else
  echo "track_edit_check: the plain run ..."
  R=$OUT/rc_plain
  V2=$(run "$R")
fi
case $V2 in "race_check: PASS"*) ;; *) fail "plain run: $V2" ;; esac
ROUTE=$(grep -o '^\[b3live\] route [^ ]* from the game' "$R/native_play.log" | head -1 | sed 's/^\[b3live\] route //; s/ from the game$//')
[ -n "$ROUTE" ] || fail "no route line in $R/native_play.log"
KEY=$(echo "$ROUTE" | tr '\\' '/' | awk -F/ '{print toupper($(NF-1)) "/" toupper($NF)}')
echo "track_edit_check: the race's route: $ROUTE ($KEY)"

# ---- the test mod ----
rm -rf "$OUT/export" "$MODS_DIR" "$OUT/import.txt"
mkdir -p "$MOD"
cat > "$MOD/mod.ini" <<'INI'
[mod]
id = proof-track-edit
name = Proof: the race's route without green in its vertex colours
version = 1
author = track_edit_check.sh
description = Every section of the race's route with the green taken out of its vertex colours, every mesh restripped (a test; made from your own disc, never shared).
api = 1
INI
"$MT" track export "$GAME" "$KEY" "$OUT/export" > "$OUT/export.txt" 2>&1 || fail "track export: $(tail -1 "$OUT/export.txt")"
python3 -I - "$OUT/export" <<'PY' || fail "the test edit"
import json, os, sys
root = sys.argv[1]
gltf = [f for f in os.listdir(root) if f.endswith('.gltf')]
assert len(gltf) == 1, gltf
g = json.load(open(os.path.join(root, gltf[0])))
binp = os.path.join(root, g['buffers'][0]['uri'])
b = bytearray(open(binp, 'rb').read())
meshes = prims = verts = 0
for m in g['meshes']:
    if not m['name'].startswith('section_'):
        continue
    meshes += 1
    for p in m['primitives']:
        c = p['attributes'].get('COLOR_0')
        if c is None:
            continue
        a = g['accessors'][c]
        assert a['componentType'] == 5121 and a['type'] == 'VEC4', a   # normalized unsigned bytes, as the export writes them
        v = g['bufferViews'][a['bufferView']]
        at = v.get('byteOffset', 0) + a.get('byteOffset', 0)
        for i in range(a['count']):
            b[at + 4 * i + 1] = 0   # green
        prims += 1
        verts += a['count']
open(binp, 'wb').write(b)
print('track_edit_check: %d section meshes, %d primitives, %d vertices: green taken out of COLOR_0' % (meshes, prims, verts))
PY
RS=${RESTRIP:-1}
if [ "$RS" = 1 ]; then
  if ! "$MT" track import "$GAME" "$OUT/export" "$MOD" --restrip > "$OUT/import.txt" 2>&1; then
    echo "track_edit_check: --restrip refused ($(tail -1 "$OUT/import.txt")); the colour edit alone"
    RS=0
  fi
fi
if [ "$RS" = 0 ]; then
  "$MT" track import "$GAME" "$OUT/export" "$MOD" > "$OUT/import.txt" 2>&1 || fail "track import: $(tail -1 "$OUT/import.txt")"
fi
echo "track_edit_check: $(grep '^track import:' "$OUT/import.txt")"
"$MT" verify "$MOD" --game "$GAME" > "$OUT/verify.txt" 2>&1 || fail "verify: $(tail -1 "$OUT/verify.txt")"
NPATCH=$(find "$MOD/patches" -name '*.b3patch' | wc -l)

# ---- the modded run ----
echo "track_edit_check: the modded run ..."
V1=$(run "$OUT/rc_mod" proof-track-edit)
why=""
case $V1 in "race_check: PASS"*) ;; *) why="$why; modded run: $V1" ;; esac
L1=$OUT/rc_mod/native_play.log
L2=$R/native_play.log
SET=$(grep -o '^native_play: mods: active (--mods; the first wins): proof-track-edit 1 ([0-9]* files\?); set hash [0-9a-f]\{16\}' "$L1" | grep -o '[0-9a-f]\{16\}$')
[ -n "$SET" ] || why="$why; no mod set line in the modded log"
FILES=$(grep -o "^mods: TRACKS/$KEY/[A-Z]*\.DAT from proof-track-edit" "$L1" | sort -u | sed 's/^mods: //; s/ from proof-track-edit$//' | tr '\n' ' ')
grep -q "^mods: TRACKS/$KEY/STREAMED.DAT from proof-track-edit" "$L1" || why="$why; the game did not read TRACKS/$KEY/STREAMED.DAT from the mod"
grep -q "^\[b3render\] mods: proof-track-edit 1 ([0-9]* files\?); set hash ${SET:-x} over " "$L1" || why="$why; the renderer did not build the same set"
grep -q '^\[b3render\] mods: WARNING' "$L1" && why="$why; the renderer's set hash differs from the game's"
grep -q '^native_play: mods: none' "$L2" || why="$why; the plain run's log has no 'mods: none'"
grep -q "^\[b3live\] route $ROUTE from the game" "$L1" || why="$why; the modded run drew another route"
python3 -I - "$OUT" "$R/shots" "$OUT/rc_mod/shots" $RACE_SHOTS > "$OUT/diff.txt" <<'PY' || why="$why; $(tail -1 "$OUT/diff.txt")"
import struct, sys, zlib
out, plain, mod, shots = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4:]
def png(path):
    d = open(path, 'rb').read()
    assert d[:8] == b'\x89PNG\r\n\x1a\n', path
    p, idat, w = 8, b'', 0
    while p < len(d):
        n, t = struct.unpack_from('>I4s', d, p)
        c = d[p + 8:p + 8 + n]
        if t == b'IHDR':
            w, h, depth, ct = struct.unpack_from('>IIBB', c)
            assert depth == 8 and ct in (2, 6), (path, depth, ct)
            bpp = 4 if ct == 6 else 3
        elif t == b'IDAT':
            idat += c
        p += 12 + n
    raw, stride = zlib.decompress(idat), w * bpp
    px, prev = bytearray(), bytearray(stride)
    for y in range(h):
        f, line = raw[y * (stride + 1)], bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            a = line[i - bpp] if i >= bpp else 0
            b, c = prev[i], prev[i - bpp] if i >= bpp else 0
            if f == 1: line[i] = (line[i] + a) & 255
            elif f == 2: line[i] = (line[i] + b) & 255
            elif f == 3: line[i] = (line[i] + (a + b) // 2) & 255
            elif f == 4:
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        px += line
        prev = line
    return w, h, bpp, px
def write_png(path, w, h, rgb):
    raw = b''.join(b'\0' + bytes(rgb[y * w * 3:(y + 1) * w * 3]) for y in range(h))
    chunk = lambda t, c: struct.pack('>I', len(c)) + t + c + struct.pack('>I', zlib.crc32(t + c) & 0xffffffff)
    open(path, 'wb').write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) +
                           chunk(b'IDAT', zlib.compress(raw, 6)) + chunk(b'IEND', b''))
bad = False
for q in shots:
    w, h, bpp, a = png('%s/live_%s_0.png' % (plain, q))
    w2, h2, bpp2, b = png('%s/live_%s_0.png' % (mod, q))
    assert (w, h) == (w2, h2)
    changed = toward = 0
    x0, y0, x1, y1 = w, h, -1, -1
    mask = bytearray(w * h * 3)
    for i in range(w * h):
        pa, pb = a[i * bpp:i * bpp + 3], b[i * bpp2:i * bpp2 + 3]
        if pa == pb:
            g = pa[1] // 4   # the unchanged picture, dimmed
            mask[i * 3:i * 3 + 3] = bytes((g, g, g))
            continue
        changed += 1
        ok = pb[1] <= pa[1]   # less green (or as much)
        toward += ok
        mask[i * 3:i * 3 + 3] = bytes((255, 0, 255)) if ok else bytes((0, 255, 0))
        x, y = i % w, i // w
        x0, y0, x1, y1 = min(x0, x), min(y0, y), max(x1, x), max(y1, y)
    write_png('%s/diff_%s.png' % (out, q), w, h, mask)
    share = toward / changed if changed else 0
    print('shot %s: %d of %d pixels changed (%.2f %%), %.1f %% of them less green; changed area x %d..%d, y %d..%d; mask %s/diff_%s.png'
          % (q, changed, w * h, 100.0 * changed / (w * h), 100 * share, x0, x1, y0, y1, out, q))
    if not changed or share < 0.9:
        bad = True
if bad:
    print('a race shot did not change, or changed in other colours too')
    sys.exit(1)
PY
cat "$OUT/diff.txt"
if [ -n "${EVIDENCE:-}" ]; then
  mkdir -p "$EVIDENCE"
  for q in $RACE_SHOTS; do
    cp "$R/shots/live_${q}_0.png" "$EVIDENCE/race_${q}_plain.png"
    cp "$OUT/rc_mod/shots/live_${q}_0.png" "$EVIDENCE/race_${q}_edited_track.png"
    cp "$OUT/diff_$q.png" "$EVIDENCE/race_${q}_diff.png"
  done
  cp "$OUT/diff.txt" "$EVIDENCE/diff.txt"
  { echo "$V2"; echo "$V1"; grep '^track import:' "$OUT/import.txt"; grep '^native_play: mods: active' "$L1"; grep "^mods: TRACKS/" "$L1" | sort -u; grep '^\[b3render\] mods:' "$L1"; } > "$EVIDENCE/log_lines.txt"
fi
if [ -z "$why" ]; then
  echo "track_edit_check: PASS the game and the renderer used the edited route from the mod ($FILES($NPATCH patches, restrip $RS); set hash $SET); race shots $RACE_SHOTS changed, towards less green (masks $OUT/diff_*.png; logs $L1, $L2)"
else
  echo "track_edit_check: FAIL ${why#; }"
  exit 1
fi
