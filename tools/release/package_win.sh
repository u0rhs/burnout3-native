#!/bin/bash
# package_win.sh -- the Windows test build for testers, as one zip (WSL):
#   burnout3-native-<version>-win64.zip under $REL (default $WIN/release, outside the repo) holding
#     burnout3-native-<version>-win64/
#       Burnout3Launcher.exe  native_play.exe  b3render.dll  SDL3.dll  b3-icon.png  b3modtool.exe
#       b3_car_tools.zip  (the Blender add-on for car mods, a front end on b3modtool: tools/blender/make_addon_zip.py)
#       README-TESTERS.txt  KNOWN-ISSUES.txt  VERSION.txt
#       LICENSES/  (THIRD-PARTY-NOTICES.txt and the license texts: SDL3, zlib, stb_truetype, the decoders, Lua, libchdr
#                  with LZMA SDK / miniz / zstd, MinGW-w64, GCC runtime)
#   No developer tools (b3view, b3replay, b3assetdump, gameview_replay, native_boot), no game data, no saves.
#
#   tools/release/package_win.sh              builds first: tools/native/build_win.sh RENDER=1 STRIP=1 into
#                                             $WIN/release-build (the ~40-minute native build; JOBS as there)
#   OUT=<folder> tools/release/package_win.sh packages a finished build (native_play.exe beside b3render.dll,
#                                             SDL3.dll, Burnout3Launcher.exe, b3-icon.png); the copies are stripped
#                                             here with the toolchain's strip.exe (SDL3.dll is left as shipped)
# The version is the one compiled into native_play.exe (tools/release/version_info.sh's string); the zip is named
# after it. A crash report's offsets (native_play's "#k +0x..." lines) need the unstripped build of the same commit:
# keep it (or rebuild that commit) when a tester sends one.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
PORT=$(cd "$HERE/../.." && pwd)
. "$PORT/tools/local_env.sh"; b3_local_env "$PORT"   # the machine's paths (local.env)
WIN=${WIN:-$PORT/build-win}   # local.env: where the Windows toolchain (toolchain/mingw64) and outputs live
TC=$WIN/toolchain/mingw64
SDL=${SDL3_MINGW:-$(dirname "$PORT")/ext/sdl3-3.4.2-mingw64/mingw64}
REL=${REL:-$WIN/release}
die() { echo "package_win: $*" >&2; exit 1; }

(cd "$TC" && sha256sum --quiet -c "$PORT/tools/toolchain/toolchain.sha256") || die "toolchain does not match toolchain.sha256"
if [ -z "${OUT:-}" ]; then
    OUT=$WIN/release-build
    echo "package_win: building into $OUT (tools/native/build_win.sh RENDER=1 STRIP=1) ..."
    OUT="$OUT" RENDER=1 STRIP=1 "$PORT/tools/native/build_win.sh"
fi
FILES=(Burnout3Launcher.exe native_play.exe b3render.dll SDL3.dll b3-icon.png b3modtool.exe)   # b3modtool: the mod kit's asset tool (docs/modding/b3modtool.md)
for f in "${FILES[@]}"; do [ -f "$OUT/$f" ] || die "no $OUT/$f (OUT=<a build with RENDER=1>)"; done

# the version the build carries (version.h: "<base>+<commit>[-dirty]", from the repo's VERSION base)
BASE=$(tr -d ' \r\n' < "$PORT/VERSION")
# the full stamp first ("<base>+<commit>"): a bare "<base>" can also be other text in a program (1.2.0: the memory
# card's "Sony PS2 Memory Card Format 1.2.0.0" in the launcher's save import)
stamp() {
    local s
    s=$(grep -a -o -E "${BASE//./\\.}\+[0-9a-f]+(-dirty)?" "$1" | head -1 || true)
    [ -n "$s" ] || s=$(grep -a -o -m1 -E "${BASE//./\\.}" "$1" | head -1 || true)
    printf '%s' "$s"
}
VER=$(stamp "$OUT/native_play.exe")
[ -n "$VER" ] || die "no version string ($BASE...) in $OUT/native_play.exe: a build from before the version stamp?"
# nofallback1: a release runs no PS2 code -- no R5900 interpreter in native_play.exe (build_win.sh NATIVE_FALLBACK=0)
grep -a -q 'r5900_mini: \|ran retail in the mini' "$OUT/native_play.exe" && die "native_play.exe has the retail fallback in it (a NATIVE_FALLBACK=1 build)"
LVER=$(stamp "$OUT/Burnout3Launcher.exe")
[ "$LVER" = "$VER" ] || die "native_play.exe is $VER but Burnout3Launcher.exe is ${LVER:-unstamped}: build them together"
case "$VER" in *-dirty) echo "package_win: WARNING: $VER was built from a tree with uncommitted changes" >&2;; esac
NAME=burnout3-native-$(echo "$VER" | tr -c 'A-Za-z0-9._\n-' '-')-win64
echo "package_win: $NAME"

ST=$REL/stage/$NAME
python3 -I -c 'import shutil,sys; shutil.rmtree(sys.argv[1], ignore_errors=True)' "$REL/stage"
mkdir -p "$ST/LICENSES"
for f in "${FILES[@]}"; do cp "$OUT/$f" "$ST/"; done
python3 -I "$PORT/tools/blender/make_addon_zip.py" "$ST/b3_car_tools.zip" > /dev/null || die "make_addon_zip failed"   # blender1

# strip the port's own programs (a no-op on a STRIP=1 build) with the toolchain's strip.exe, through cmd.exe as the
# build scripts run the toolchain
WTC=$(wslpath -w "$TC")
printf '@echo off\r\nset "PATH=%s\\bin;%%SystemRoot%%\\system32;%%SystemRoot%%"\r\n"%s\\bin\\%%1" @%%2\r\n' "$WTC" "$WTC" > "$REL/stage/tc.cmd"
for f in Burnout3Launcher.exe native_play.exe b3render.dll b3modtool.exe; do
    printf '%s\n' --strip-unneeded "$(wslpath -m "$ST/$f")" > "$REL/stage/strip.rsp"
    (cd "$REL/stage" && cmd.exe /c "$(wslpath -w "$REL/stage/tc.cmd")" strip.exe "$(wslpath -w "$REL/stage/strip.rsp")" < /dev/null) \
        || die "strip.exe failed on $f"
done
rm -f "$REL/stage/tc.cmd" "$REL/stage/strip.rsp"

# the texts: @VERSION@ filled in, Windows line ends
SDLV=$(sed -n 's/^Version: *//p' "$SDL/lib/pkgconfig/sdl3.pc" 2>/dev/null | head -1)
text() {   # text <source> <dest>
    sed -e "s/@VERSION@/$VER/g" -e "s/@SDL_VERSION@/${SDLV:-3}/g" -e 's/$/\r/' "$1" > "$2"
}
text "$HERE/README-TESTERS.txt" "$ST/README-TESTERS.txt"
text "$HERE/KNOWN-ISSUES.txt" "$ST/KNOWN-ISSUES.txt"
text "$HERE/THIRD-PARTY-NOTICES.txt" "$ST/LICENSES/THIRD-PARTY-NOTICES.txt"
printf 'Burnout 3 native port %s\r\ntest build for Windows (64-bit), packaged %s\r\n' "$VER" "$(date -u +%Y-%m-%d)" > "$ST/VERSION.txt"
L=$TC/share/licenses
lic() { [ -f "$1" ] || die "no license file $1"; sed 's/$/\r/' "$1" > "$ST/LICENSES/$2"; }
lic "$SDL/share/licenses/SDL3/LICENSE.txt" SDL3-LICENSE.txt
lic "$L/zlib/LICENSE" zlib-LICENSE.txt
lic "$L/crt/COPYING.MinGW-w64-runtime.txt" mingw-w64-runtime-COPYING.txt
lic "$L/winpthreads/COPYING" winpthreads-COPYING.txt
lic "$L/libstdc++/COPYING.RUNTIME" GCC-RUNTIME-LIBRARY-EXCEPTION.txt
# stb_truetype's license is the end of its header (the block from "This software is available under 2 licenses")
awk '/This software is available under 2 licenses/{on=1} on{print} /^\*\//{if(on) exit}' "$PORT/src/b3launcher/third_party/stb_truetype.h" \
    | sed 's/$/\r/' > "$ST/LICENSES/stb_truetype-LICENSE.txt"
[ -s "$ST/LICENSES/stb_truetype-LICENSE.txt" ] || die "no license block in stb_truetype.h"
# the custom soundtrack's decoders (src/native/third_party: dr_mp3 / dr_flac / dr_wav, minimp3 inside dr_mp3, stb_vorbis)
lic "$PORT/src/native/third_party/LICENSE-dr_libs.txt" dr_libs-LICENSE.txt
lic "$PORT/src/native/third_party/LICENSE-minimp3.txt" minimp3-LICENSE.txt
lic "$PORT/src/native/third_party/LICENSE-stb_vorbis.txt" stb_vorbis-LICENSE.txt
lic "$PORT/src/third_party/lua-5.4.9/LICENSE.txt" Lua-LICENSE.txt   # lua6: script mods (src/native/script_mods.h)
# chd1: .chd disc images (src/native/third_party/libchdr: libchdr and the LZMA SDK, miniz and zstd decoders in it)
lic "$PORT/src/native/third_party/libchdr/LICENSE-libchdr.txt" libchdr-LICENSE.txt
lic "$PORT/src/native/third_party/libchdr/LICENSE-lzma.txt" lzma-LICENSE.txt
lic "$PORT/src/native/third_party/libchdr/LICENSE-miniz.txt" miniz-LICENSE.txt
lic "$PORT/src/native/third_party/libchdr/LICENSE-zstd.txt" zstd-LICENSE.txt

# ---- privacy: no trace of the build machine's user (its account name or home folder) in anything packaged ----
privacy_scan() {   # $1: the staged folder
  local who home hits
  who=$(id -un 2>/dev/null || true); home=${HOME:-}
  hits=""
  if [ "${#who}" -ge 3 ]; then hits=$(grep -r -a -l -i -F -- "$who" "$1" 2>/dev/null || true); fi
  if [ -z "$hits" ] && [ "${#home}" -ge 6 ]; then hits=$(grep -r -a -l -F -- "$home" "$1" 2>/dev/null || true); fi
  [ -z "$hits" ] || die "the package would carry the build machine's user name or home folder (anonymous packages only):
$hits"
  echo "$(basename "$0" .sh): privacy scan: no user name or home folder of this machine in the package"
}
privacy_scan "$ST"

ZIP=$REL/$NAME.zip
python3 -I - "$REL/stage" "$NAME" "$ZIP" <<'EOF'
import os, sys, zipfile
root, name, out = sys.argv[1], sys.argv[2], sys.argv[3]
tmp = out + '.tmp'
with zipfile.ZipFile(tmp, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
    for d, _, files in os.walk(os.path.join(root, name)):
        for f in sorted(files):
            p = os.path.join(d, f)
            z.write(p, os.path.relpath(p, root).replace(os.sep, '/'))
os.replace(tmp, out)
EOF
python3 -I -c 'import sys, zipfile; z = zipfile.ZipFile(sys.argv[1]); bad = z.testzip(); sys.exit("bad entry %s" % bad if bad else 0)' "$ZIP"
echo "package_win: $(wslpath -w "$ZIP") ($(du -h "$ZIP" | cut -f1)):"
python3 -I -c 'import sys, zipfile
for i in zipfile.ZipFile(sys.argv[1]).infolist(): print("  %9d  %s" % (i.file_size, i.filename))' "$ZIP"
