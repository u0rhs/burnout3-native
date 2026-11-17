#!/bin/bash
# car_mod_check.sh -- an edited car in the game (mod kit part 3c, the car import: docs/modding/b3modtool.md "car
# import"): a test mod made here with `b3modtool car export` / `car import` from the user's own disc -- every player
# car's LOD 3 body and panels (the L0 and A0..A5 meshes) scaled 1.3x about the car's origin and its paint pictures
# recoloured (red and blue swapped) in the exported glTF / PNGs, then imported as b3patches -- loaded by native_play through --mods in an
# offscreen race_check run, against the same run without it. Every player car is edited, so the race's own car and
# its rivals are, whichever the save picks.
#
#   tools/modding/car_mod_check.sh [native_play] [out dir]
#     native_play   default build/native_play
#     out dir       default build/car_mod_check (the exports, the test mod, both runs' logs and shots, the difference
#                   masks; game data: delete it after)
#   Environment: B3_GAME_DIR (a folder of the disc's files), SAVES (as race_check.sh), MODTOOL (default
#   build/b3modtool), REF (a no-mod race_check out dir of the same build: that run is not repeated), EVIDENCE (a folder
#   the race shots of both runs and the masks are copied to), RENDER_EXTRA (passed on to race_check, both runs)
#
# PASS (last line "car_mod_check: PASS ...") when every car imported (b3modtool's own checks: the new file decodes to
# what was meant), both runs PASS race_check, the modded log names the set and the game read car files from the mod's
# patches, the renderer built the same set, the plain run has no mods, and each race shot changed.
set -u
cd "$(dirname "$0")/../.." || exit 1
NP=${1:-build/native_play}
OUT=${2:-build/car_mod_check}
MT=${MODTOOL:-build/b3modtool}
. tools/local_env.sh; b3_local_env .
GAME=${B3_GAME_DIR:-data/game}
RACE_SHOTS="10700 11500"
fail() { echo "car_mod_check: FAIL $*"; exit 1; }
[ -x "$NP" ] || fail "no $NP"
[ -x "$MT" ] || fail "no $MT (make modtool)"
[ -d "$GAME" ] || fail "B3_GAME_DIR ($GAME) is not a folder of the disc's files"
case $OUT in /*) ;; *) OUT=$PWD/$OUT ;; esac
mkdir -p "$OUT"
MODS_DIR=$OUT/mods
MOD=$MODS_DIR/proof-car

# ---- the test mod ----
rm -rf "$OUT/export" "$MODS_DIR" "$OUT/import.txt"
mkdir -p "$MOD"
cat > "$MOD/mod.ini" <<'INI'
[mod]
id = proof-car
name = Proof: every car's body 1.3x, paints recoloured
version = 1
author = car_mod_check.sh
description = The cars' LOD 3 bodies and panels scaled 1.3x and their paints red <-> blue (a test; made from your own disc, never shared).
api = 1
INI
"$MT" car export "$GAME" all "$OUT/export" > "$OUT/export.txt" 2>&1 || fail "car export: $(tail -1 "$OUT/export.txt")"
python3 -I - "$OUT/export" <<'PY' || fail "the test edit"
import json, os, struct, sys, zlib
root = sys.argv[1]
def png_rw(path, fn):
    d = open(path, 'rb').read()
    p, idat, chunks = 8, b'', []
    while p < len(d):
        n, t = struct.unpack_from('>I4s', d, p)
        c = d[p + 8:p + 8 + n]
        if t == b'IHDR': w, h, depth, ct = struct.unpack_from('>IIBB', c)
        elif t == b'IDAT': idat += c
        p += 12 + n
    assert depth == 8 and ct == 6, path
    raw, stride, bpp = zlib.decompress(idat), w * 4, 4
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
    fn(px)
    rawo = b''.join(b'\0' + bytes(px[y * stride:(y + 1) * stride]) for y in range(h))
    chunk = lambda t, c: struct.pack('>I', len(c)) + t + c + struct.pack('>I', zlib.crc32(t + c) & 0xffffffff)
    open(path, 'wb').write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0)) +
                           chunk(b'IDAT', zlib.compress(rawo, 6)) + chunk(b'IEND', b''))
def swap(px):
    px[0::4], px[2::4] = px[2::4], px[0::4]
cars = 0
for d in sorted(os.listdir(root)):
    folder = os.path.join(root, d)
    if not os.path.isdir(folder):
        continue
    g = json.load(open(os.path.join(folder, d + '.gltf')))
    binp = os.path.join(folder, d + '.bin')
    b = bytearray(open(binp, 'rb').read())
    # each panel (A0..A5) sits at its part node's frame M (column-major): scaled about the car's origin, its vertices
    # become M^-1 (1.3 M p); the body (L0) has no frame
    frame = {}
    for nd in g['nodes']:
        nm = nd.get('name', '')
        if nm.startswith('LOD3_A') and len(nm) == 7:
            frame[nm[5:]] = nd.get('matrix', [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1])
    def inv_affine(M):
        r = [[M[c * 4 + k] for c in range(3)] for k in range(3)]   # the 3x3 by rows
        det = (r[0][0] * (r[1][1] * r[2][2] - r[1][2] * r[2][1]) - r[0][1] * (r[1][0] * r[2][2] - r[1][2] * r[2][0])
               + r[0][2] * (r[1][0] * r[2][1] - r[1][1] * r[2][0]))
        inv = [[(r[(j + 1) % 3][(i + 1) % 3] * r[(j + 2) % 3][(i + 2) % 3] - r[(j + 1) % 3][(i + 2) % 3] * r[(j + 2) % 3][(i + 1) % 3]) / det
                for j in range(3)] for i in range(3)]
        t = [M[12], M[13], M[14]]
        return inv, [-sum(inv[i][k] * t[k] for k in range(3)) for i in range(3)]
    n = 0
    for m in g['meshes']:
        part = m['name'][5:7]
        if not m['name'].startswith('LOD3_') or not (part == 'L0' or part in frame):
            continue
        a = g['accessors'][m['primitives'][0]['attributes']['POSITION']]
        v = g['bufferViews'][a['bufferView']]
        at = v.get('byteOffset', 0) + a.get('byteOffset', 0)
        M = frame.get(part)
        if M:
            inv, it = inv_affine(M)
        for i in range(a['count']):
            p = list(struct.unpack_from('<3f', b, at + 12 * i))
            if M:
                w = [1.3 * (M[0 + k] * p[0] + M[4 + k] * p[1] + M[8 + k] * p[2] + M[12 + k]) for k in range(3)]
                p = [sum(inv[k][j] * w[j] for j in range(3)) + it[k] for k in range(3)]
            else:
                p = [1.3 * x for x in p]
            struct.pack_into('<3f', b, at + 12 * i, *p)
        n += 1
    assert n, d
    open(binp, 'wb').write(b)
    for f in os.listdir(folder):
        if '_paint' in f and f.endswith('.png'):
            png_rw(os.path.join(folder, f), swap)
    cars += 1
print('car_mod_check: %d cars edited (LOD 3 body and panels x 1.3 about the car origin, paints red <-> blue)' % cars)
PY
n=0
for d in "$OUT"/export/*/; do
  "$MT" car import "$GAME" "$d" "$MOD" >> "$OUT/import.txt" 2>&1 || fail "car import $(basename "$d"): $(tail -1 "$OUT/import.txt")"
  n=$((n + 1))
done
echo "car_mod_check: $n cars imported into $MOD ($(ls "$MOD"/patches/PVEH/*/*.b3patch | wc -l) patches); $(grep -c 'edited in place' "$OUT/import.txt") reports"
"$MT" verify "$MOD" --game "$GAME" > "$OUT/verify.txt" 2>&1 || fail "verify: $(tail -1 "$OUT/verify.txt")"

# ---- the runs ----
run() {   # run <out dir> [MODS]
  local dir=$1
  env ${2:+MODS=$2 MODS_DIR=$MODS_DIR} tools/native/race_check.sh "$NP" "$dir" > "$dir.verdict.txt" 2>&1
  tail -1 "$dir.verdict.txt"
}
if [ -n "${REF:-}" ]; then R=$REF; V2="race_check: PASS (REF $REF, not checked here)"
else
  echo "car_mod_check: the plain run ..."
  R=$OUT/rc_plain
  V2=$(run "$R")
fi
case $V2 in "race_check: PASS"*) ;; *) fail "plain run: $V2" ;; esac
echo "car_mod_check: the modded run ..."
V1=$(run "$OUT/rc_mod" proof-car)
why=""
case $V1 in "race_check: PASS"*) ;; *) why="$why; modded run: $V1" ;; esac
L1=$OUT/rc_mod/native_play.log
L2=$R/native_play.log
SET=$(grep -o '^native_play: mods: active (--mods; the first wins): proof-car 1 ([0-9]* files\?); set hash [0-9a-f]\{16\}' "$L1" | grep -o '[0-9a-f]\{16\}$')
[ -n "$SET" ] || why="$why; no mod set line in the modded log"
CARS=$(grep -o '^mods: PVEH/[A-Z]*/CAR[0-9]*\.BGV from proof-car' "$L1" | sort -u | sed 's/^mods: //; s/ from proof-car$//' | tr '\n' ' ')
[ -n "$CARS" ] || why="$why; the game read no car file from the mod"
grep -q "^\[b3render\] mods: proof-car 1 ([0-9]* files\?); set hash ${SET:-x} over " "$L1" || why="$why; the renderer did not build the same set"
grep -q '^\[b3render\] mods: WARNING' "$L1" && why="$why; the renderer's set hash differs from the game's"
grep -q '^native_play: mods: none' "$L2" || why="$why; the plain run's log has no 'mods: none'"
python3 -I - "$OUT" "$R/shots" "$OUT/rc_mod/shots" $RACE_SHOTS > "$OUT/diff.txt" <<'PY' || why="$why; $(tail -1 "$OUT/diff.txt")"
import struct, sys, zlib
out, plain, mod, shots = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4:]
def png(path):
    d = open(path, 'rb').read()
    p, idat = 8, b''
    while p < len(d):
        n, t = struct.unpack_from('>I4s', d, p)
        c = d[p + 8:p + 8 + n]
        if t == b'IHDR':
            w, h, depth, ct = struct.unpack_from('>IIBB', c)
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
    changed = 0
    x0, y0, x1, y1 = w, h, -1, -1
    mask = bytearray(w * h * 3)
    for i in range(w * h):
        pa, pb = a[i * bpp:i * bpp + 3], b[i * bpp2:i * bpp2 + 3]
        if pa == pb:
            g = pa[1] // 4
            mask[i * 3:i * 3 + 3] = bytes((g, g, g))
            continue
        changed += 1
        mask[i * 3:i * 3 + 3] = bytes((255, 255, 0))
        x, y = i % w, i // w
        x0, y0, x1, y1 = min(x0, x), min(y0, y), max(x1, x), max(y1, y)
    write_png('%s/diff_%s.png' % (out, q), w, h, mask)
    print('shot %s: %d of %d pixels changed (%.2f %%), changed area x %d..%d, y %d..%d; mask %s/diff_%s.png'
          % (q, changed, w * h, 100.0 * changed / (w * h), x0, x1, y0, y1, out, q))
    bad = bad or not changed
if bad:
    print('a race shot did not change')
    sys.exit(1)
PY
cat "$OUT/diff.txt"
if [ -n "${EVIDENCE:-}" ]; then
  mkdir -p "$EVIDENCE"
  for q in $RACE_SHOTS; do
    cp "$R/shots/live_${q}_0.png" "$EVIDENCE/race_${q}_plain.png"
    cp "$OUT/rc_mod/shots/live_${q}_0.png" "$EVIDENCE/race_${q}_edited_car.png"
    cp "$OUT/diff_$q.png" "$EVIDENCE/race_${q}_diff.png"
  done
  cp "$OUT/diff.txt" "$EVIDENCE/diff.txt"
  { echo "$V2"; echo "$V1"; grep '^native_play: mods: active' "$L1"; grep '^mods: PVEH/' "$L1" | sort -u; grep '^\[b3render\] mods:' "$L1"; } > "$EVIDENCE/log_lines.txt"
fi
if [ -z "$why" ]; then
  echo "car_mod_check: PASS the game and the renderer used the edited cars from the mod ($CARS; set hash $SET); race shots $RACE_SHOTS changed (masks $OUT/diff_*.png; logs $L1, $L2)"
else
  echo "car_mod_check: FAIL ${why#; }"
  exit 1
fi
