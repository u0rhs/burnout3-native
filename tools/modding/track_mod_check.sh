#!/bin/bash
# track_mod_check.sh -- the renderer reads the mods' files too (mod kit part 1b, docs/modding/DESIGN.md §1.2): a test
# mod made here from a COPY of the user's own STATIC.DAT of the route the race check drives, with one texture's palette
# set to magenta, loaded by native_play through --mods in an offscreen race_check run, against the same run without it.
# The renderer draws the route from its own copy of STATIC.DAT (found by the header the game loaded); with the mod it
# must be the mod's copy, so the race shots change, and only where that texture is (the changed pixels move towards
# magenta).
#
#   tools/modding/track_mod_check.sh [native_play] [out dir]
#     native_play   default build/native_play
#     out dir       default build/track_mod_check (the test mod, both runs' logs and shots, the difference masks; game
#                   data: delete it after)
#   Environment: B3_GAME_DIR (a folder of the disc's files), SAVES (as race_check.sh), TEX (the texture's name in
#   STATIC.DAT; default: the largest paletted one), REF (a no-mod race_check out dir of the same build: that run is not
#   repeated), RENDER_EXTRA (passed on to race_check, both runs)
#
# PASS (last line "track_mod_check: PASS ...") when both runs PASS race_check; the modded run's log names the set, the
# game read the route's STATIC.DAT from the mod and the renderer built the same set (the same set hash, no warning);
# the plain run's log has "mods: none" and no renderer mod line; both runs drew the same route; each race shot differs,
# and at least 90 % of the changed pixels moved towards magenta (red and blue up or equal, green down or equal).
set -u
cd "$(dirname "$0")/../.." || exit 1
NP=${1:-build/native_play}
OUT=${2:-build/track_mod_check}
. tools/local_env.sh; b3_local_env .
GAME=${B3_GAME_DIR:-data/game}
RACE_SHOTS="10700 11500"
fail() { echo "track_mod_check: FAIL $*"; exit 1; }
[ -x "$NP" ] || fail "no $NP"
[ -d "$GAME" ] || fail "B3_GAME_DIR ($GAME) is not a folder of the disc's files"
case $OUT in /*) ;; *) OUT=$PWD/$OUT ;; esac
mkdir -p "$OUT"

run() {   # run <out dir> [MODS]
  local dir=$1
  env ${2:+MODS=$2 MODS_DIR=$MODS_DIR} tools/native/race_check.sh "$NP" "$dir" > "$dir.verdict.txt" 2>&1
  tail -1 "$dir.verdict.txt"
}
MODS_DIR=$OUT/mods
if [ -n "${REF:-}" ]; then R=$REF; V2="race_check: PASS (REF $REF, not checked here)"
else
  echo "track_mod_check: the plain run ..."
  R=$OUT/rc_plain
  V2=$(run "$R")
fi
case $V2 in "race_check: PASS"*) ;; *) fail "plain run: $V2" ;; esac
ROUTE=$(grep -o '^\[b3live\] route [^ ]* from the game' "$R/native_play.log" | head -1 | sed 's/^\[b3live\] route //; s/ from the game$//')
[ -n "$ROUTE" ] || fail "no route line in $R/native_play.log"
echo "track_mod_check: the race's route: $ROUTE"

rm -rf "$MODS_DIR"
mkdir -p "$MODS_DIR/proof-track"
cat > "$MODS_DIR/proof-track/mod.ini" <<'INI'
[mod]
id = proof-track
name = Proof: one track texture in magenta
version = 1
author = track_mod_check.sh
description = One texture of the race's route with a magenta palette (a test; made from your own disc, never shared).
api = 1
INI
REL=$(python3 -I - "$GAME" "$ROUTE" "$MODS_DIR/proof-track/data" "${TEX:-}" <<'PY'
import os, struct, sys
game, route, data, want = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4]
rel = os.path.relpath(os.path.join(route, 'STATIC.DAT'), game)
if rel.startswith('..'):
    sys.exit('the route %s is not below %s' % (route, game))
src = None
for f in os.listdir(route):
    if f.upper() == 'STATIC.DAT':
        src = os.path.join(route, f)
if not src:
    sys.exit('no STATIC.DAT in ' + route)
b = bytearray(open(src, 'rb').read())
u16 = lambda o: struct.unpack_from('<H', b, o)[0]
u32 = lambda o: struct.unpack_from('<I', b, o)[0]
u64 = lambda o: struct.unpack_from('<Q', b, o)[0]

def gif(at, qwc):
    # the GIF packets at `at`: yields ('ad', addr, data) and ('image', offset, bytes)
    q = 0
    while q < qwc:
        lo, regs = u64(at + q * 16), u64(at + q * 16 + 8)
        q += 1
        nloop, flg, nreg = lo & 0x7FFF, (lo >> 58) & 3, (lo >> 60) & 0xF or 16
        if flg == 0:
            for _ in range(nloop):
                for r in range(nreg):
                    if q >= qwc:
                        return
                    if (regs >> (4 * r)) & 0xF == 0xE:
                        yield ('ad', b[at + q * 16 + 8], u64(at + q * 16))
                    q += 1
        elif flg == 1:
            q += (nloop * nreg + 1) // 2
        else:
            n = min(nloop, qwc - q)
            yield ('image', at + q * 16, n * 16)
            q += n

ntex, tab = struct.unpack_from('<h', b, 0x16)[0], u32(0x18)
cands = []
for k in range(ntex):
    o = u32(tab + 4 * k)
    tex0 = u64(o + 0x3C)
    psm, cpsm = (tex0 >> 20) & 0x3F, (tex0 >> 51) & 0xF
    if psm not in (0x13, 0x14):   # PSMT8, PSMT4: paletted
        continue
    name = bytes(b[o + 0xA8:o + 0xC8]).split(b'\0')[0].decode('ascii', 'replace')
    pal, up = u32(o + 0x44), o + u32(o + 0x24)
    clut = None
    for t in range(u32(up)):
        e = up + 16 + 64 * t
        cnt, ref = u32(e), u32(e + 48)
        dbp = None
        for kind, a, v in gif(e + 16, cnt & 0xFFFF):
            if kind == 'ad' and a == 0x50:
                dbp = (v >> 32) & 0x3FFF   # BITBLTBUF DBP (bits 32..45)
        if dbp == pal:   # the transfer that writes the CLUT: its image data is the palette
            imgs = [(a, v) for kind, a, v in gif(o + u32(e + 52), ref & 0xFFFF) if kind == 'image']
            if len(imgs) == 1:
                clut = imgs[0]
    if clut:
        cands.append((u32(o + 0x0C) * u32(o + 0x10), name, cpsm, clut))
if not cands:
    sys.exit('no paletted texture with its own palette transfer in ' + src)
cands.sort(key=lambda c: -c[0])
pick = [c for c in cands if c[1] == want] if want else cands[:1]
if not pick:
    sys.exit('no texture named %s (paletted, own palette): %s' % (want, ', '.join(c[1] for c in cands)))
area, name, cpsm, (at, n) = pick[0]
if cpsm == 0:      # CT32: R G B A
    for i in range(at, at + n, 4):
        b[i], b[i + 1], b[i + 2] = 255, 0, 255
else:              # CT16: A1 B5 G5 R5
    for i in range(at, at + n, 2):
        struct.pack_into('<H', b, i, 0x7C1F | (u16(i) & 0x8000))
out = os.path.join(data, rel)
os.makedirs(os.path.dirname(out), exist_ok=True)
open(out, 'wb').write(b)
sys.stderr.write('track_mod_check: test mod made: %s: texture %s (%d texels, CLUT %d bytes at 0x%x set to magenta; %d paletted textures: %s)\n'
                 % (out, name, area, n, at, len(cands), ', '.join(c[1] for c in cands[:12])))
print(rel.replace(os.sep, '/'))
PY
) || fail "cannot make the test mod"
KEY=$(echo "$REL" | tr 'a-z' 'A-Z')

echo "track_mod_check: the modded run ..."
V1=$(run "$OUT/rc_mod" proof-track)
why=""
case $V1 in "race_check: PASS"*) ;; *) why="$why; modded run: $V1" ;; esac
L1=$OUT/rc_mod/native_play.log
L2=$R/native_play.log
SET=$(grep -o '^native_play: mods: active (--mods; the first wins): proof-track 1 (1 file); set hash [0-9a-f]\{16\}' "$L1" | grep -o '[0-9a-f]\{16\}$')
[ -n "$SET" ] || why="$why; no mod set line in the modded log"
grep -q "^mods: $KEY from proof-track" "$L1" || why="$why; the game did not read $KEY from the mod"
grep -q "^\[b3render\] mods: proof-track 1 (1 file); set hash ${SET:-x} over " "$L1" || why="$why; the renderer did not build the same set"
grep -q '^\[b3render\] mods: WARNING' "$L1" && why="$why; the renderer's set hash differs from the game's"
grep -q '^native_play: mods: none' "$L2" || why="$why; the plain run's log has no 'mods: none'"
grep -q '^\[b3render\] mods:' "$L2" && why="$why; the plain run's renderer has mods"
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
        dr, dg, db = pb[0] - pa[0], pb[1] - pa[1], pb[2] - pa[2]
        ok = dr >= 0 and db >= 0 and dg <= 0
        toward += ok
        mask[i * 3:i * 3 + 3] = bytes((255, 0, 255)) if ok else bytes((0, 255, 0))
        x, y = i % w, i // w
        x0, y0, x1, y1 = min(x0, x), min(y0, y), max(x1, x), max(y1, y)
    write_png('%s/diff_%s.png' % (out, q), w, h, mask)
    share = toward / changed if changed else 0
    print('shot %s: %d of %d pixels changed (%.2f %%), %.1f %% of them towards magenta; changed area x %d..%d, y %d..%d; mask %s/diff_%s.png'
          % (q, changed, w * h, 100.0 * changed / (w * h), 100 * share, x0, x1, y0, y1, out, q))
    if not changed or share < 0.9:
        bad = True
if bad:
    print('a race shot did not change, or changed in other colours too')
    sys.exit(1)
PY
cat "$OUT/diff.txt"
if [ -z "$why" ]; then
  echo "track_mod_check: PASS the renderer drew $KEY from the mod (game and renderer set hash $SET); race shots $RACE_SHOTS changed towards magenta only (masks $OUT/diff_*.png; logs $L1, $L2)"
else
  echo "track_mod_check: FAIL ${why#; }"
  exit 1
fi
