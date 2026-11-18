#!/bin/bash
# package_linux.sh -- the Linux test build for testers, as one tarball:
#   build/release/burnout3-native-<version>-linux-x86_64.tar.gz ($REL, default build/release) holding
#     burnout3-native-<version>-linux-x86_64/
#       b3launcher  native_play  libb3render.so  libSDL3.so.0  b3-icon.png  icons/hicolor/  b3modtool
#       b3_car_tools.zip  (the Blender add-on for car mods, a front end on b3modtool: tools/blender/make_addon_zip.py)
#       burnout3-native.desktop  add-to-menu.sh  README-TESTERS.txt  KNOWN-ISSUES.txt  VERSION.txt
#       LICENSES/  (THIRD-PARTY-NOTICES.txt and the license texts: SDL3, stb_truetype, dr_libs, minimp3, stb_vorbis, Lua,
#                   libchdr with LZMA SDK / miniz / zstd, the GCC runtime exception)
#   No developer tools, no game data, no saves.
#
#   tools/release/package_linux.sh        builds first: the portable SDL3 (tools/release/build_sdl3_linux.sh), then
#                                         make BUILD=build/linux-release LINUX_RELEASE=1 play launcher (JOBS, default 16)
#   OUT=<build dir> SDL3_DIR=<prefix> tools/release/package_linux.sh
#                                         packages a finished LINUX_RELEASE=1 build (native_play, libb3render.so,
#                                         b3launcher in OUT) with the libSDL3.so.0 of SDL3_DIR/lib
#   SMOKE=1                               then unpacks the tarball into $REL/smoke and runs it from there: the race
#                                         check offscreen (tools/native/race_check.sh with the packaged native_play;
#                                         B3_GAME_DIR / SAVES as there) and the launcher's screenshot mode
# Portable means: glibc 2.35 (Ubuntu 22.04) or newer, nothing else from the system but libz.so.1, the Vulkan loader
# and what SDL loads itself (X11 / Wayland, PipeWire / PulseAudio / ALSA) -- the build's flags (Makefile
# LINUX_RELEASE: include/b3compat/glibc_compat.h, static libstdc++ / libgcc, $ORIGIN RPATH) and SDL3 built for it.
# Each packaged program is checked for that (its GLIBC_ versions, no GLIBCXX, its NEEDED libraries and RPATH); one
# that fails stops the packaging. NONPORTABLE=1 packages anyway (e.g. with Ubuntu's own SDL3: SDL3_DIR=
# ../ext/sdl3-3.4.2/usr), named *-linux-x86_64-nonportable: for checks on this machine only, never for testers.
# The version is the one compiled into the programs (tools/release/version_info.sh's string; all three must carry the
# same); a "-dirty" build (uncommitted changes) is refused (ALLOW_DIRTY=1: a test package, it says so).
# The copies are stripped; the symbols go to $REL/debug/<name>/*.debug (a crash report's "#k +0x..." offsets: keep
# them, or rebuild that commit, while testers use the build).
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
PORT=$(cd "$HERE/../.." && pwd)
REL=${REL:-$PORT/build/release}
die() { echo "package_linux: $*" >&2; exit 1; }
[ "$(uname -s)-$(uname -m)" = Linux-x86_64 ] || die "Linux x86-64 only"
case "$REL" in /tmp/*) die "REL under /tmp (RAM): use the build folder";; esac

# ---- build (or take OUT=) ----
if [ -z "${OUT:-}" ]; then
    OUT=${BUILD_DIR:-$PORT/build/linux-release}
    case "$(realpath -m --relative-to="$PORT" "$OUT")" in ../*|/*) die "BUILD_DIR must be inside the repo";; esac
    SDL3_DIR=${SDL3_DIR:-$("$HERE/build_sdl3_linux.sh" "$OUT/sdl3" | tail -1)}
    [ -f "$SDL3_DIR/lib/libSDL3.so.0" ] || die "no portable SDL3 ($SDL3_DIR)"
    # objects of an older glibc_compat.h / another SDL3 are not reused (make cannot see a force-included header)
    STAMP="$(sha256sum < "$PORT/include/b3compat/glibc_compat.h" | cut -c1-16) $SDL3_DIR"
    if [ -d "$OUT" ] && [ "$(cat "$OUT/.release-stamp" 2>/dev/null)" != "$STAMP" ]; then
        echo "package_linux: the release flags changed: rebuilding $OUT's objects"
        find "$OUT" -path "$OUT/sdl3" -prune -o -name '*.o' -type f -exec rm -f {} +
        rm -f "$OUT/native_play" "$OUT/libb3render.so" "$OUT/b3launcher" "$OUT/b3modtool"
    fi
    mkdir -p "$OUT"; printf '%s' "$STAMP" > "$OUT/.release-stamp"
    echo "package_linux: building into $OUT (make LINUX_RELEASE=1 play launcher) ..."
    B=$(realpath --relative-to="$PORT" "$OUT")   # the Makefile's BUILD: relative to the repo (its object paths)
    MK=(make -C "$PORT" -j"${JOBS:-16}" BUILD="$B" LINUX_RELEASE=1 SDL3_PREFIX="$SDL3_DIR" SDL3_LIBDIR="$SDL3_DIR/lib")
    "${MK[@]}" play > "$OUT/release-build.log" 2>&1 || { tail -20 "$OUT/release-build.log" >&2; die "make play failed ($OUT/release-build.log)"; }
    "${MK[@]}" launcher >> "$OUT/release-build.log" 2>&1 || { tail -20 "$OUT/release-build.log" >&2; die "make launcher failed"; }
    "${MK[@]}" modtool >> "$OUT/release-build.log" 2>&1 || { tail -20 "$OUT/release-build.log" >&2; die "make modtool failed"; }
fi
SDL3_DIR=${SDL3_DIR:?SDL3_DIR=<the SDL3 prefix the build linked: its lib/libSDL3.so.0>}
SDLLIB=$SDL3_DIR/lib/libSDL3.so.0
[ -f "$SDLLIB" ] || SDLLIB=$SDL3_DIR/lib/x86_64-linux-gnu/libSDL3.so.0
[ -f "$SDLLIB" ] || die "no libSDL3.so.0 under $SDL3_DIR/lib"
PROGS=(b3launcher native_play libb3render.so b3modtool)   # b3modtool: the mod kit's asset tool
for f in "${PROGS[@]}"; do [ -f "$OUT/$f" ] || die "no $OUT/$f (OUT=<a LINUX_RELEASE=1 build>)"; done

# ---- the version the build carries (version.h: "<base>+<commit>[-dirty]") ----
BASE=$(tr -d ' \r\n' < "$PORT/VERSION")
# the full stamp first ("<base>+<commit>"): a bare "<base>" can also be other text in a program (1.2.0: the memory
# card's "Sony PS2 Memory Card Format 1.2.0.0" in the launcher's save import)
stamp() {
    local s
    s=$(grep -a -o -E "${BASE//./\\.}\+[0-9a-f]+(-dirty)?" "$1" | head -1 || true)
    [ -n "$s" ] || s=$(grep -a -o -m1 -E "${BASE//./\\.}" "$1" | head -1 || true)
    printf '%s' "$s"
}
VER=$(stamp "$OUT/native_play")
[ -n "$VER" ] || die "no version string ($BASE...) in $OUT/native_play"
# nofallback1: a release runs no PS2 code -- no R5900 interpreter in native_play (Makefile NATIVE_FALLBACK=0)
grep -a -q 'r5900_mini: \|ran retail in the mini' "$OUT/native_play" && die "native_play has the retail fallback in it (a NATIVE_FALLBACK=1 build)"
for f in b3launcher libb3render.so; do
    V=$(stamp "$OUT/$f")
    [ "$V" = "$VER" ] || die "native_play is $VER but $f is ${V:-unstamped}: build them together"
done
case "$VER" in
    *-dirty) [ "${ALLOW_DIRTY:-0}" = 1 ] || die "$VER was built from a tree with uncommitted changes (commit first; ALLOW_DIRTY=1: a test package)"
             echo "package_linux: WARNING: $VER was built from a tree with uncommitted changes" >&2;;
esac
HEAD=$(git -C "$PORT" rev-parse --short=8 HEAD 2>/dev/null || true)
case "$VER" in *"+$HEAD"*) ;; *) echo "package_linux: note: $VER is not this checkout's commit ($HEAD)" >&2;; esac

# ---- portability: each program's glibc versions, libraries and RPATH ----
bad=""
glibc_max() { objdump -T "$1" | grep -o 'GLIBC_[0-9.]*' | sort -uV | tail -1; }
needed() { readelf -d "$1" | sed -n 's/.*(NEEDED).*\[\(.*\)\]/\1/p'; }
check() {   # check <file> <allowed NEEDED regex> <want $ORIGIN rpath: 0|1>
    local f=$1 g n
    g=$(glibc_max "$f")
    [ "$(printf '%s\n' "$g" GLIBC_2.35 | sort -V | tail -1)" = GLIBC_2.35 ] || bad="$bad; $(basename "$f") needs $g"
    objdump -T "$f" | grep -q 'GLIBCXX_\|CXXABI_' && bad="$bad; $(basename "$f") needs the system libstdc++"
    n=$(needed "$f" | grep -vxE "$2" | tr '\n' ' ' || true)
    [ -z "$n" ] || bad="$bad; $(basename "$f") links $n"
    if [ "$3" = 1 ] && ! readelf -d "$f" | grep -q '(RPATH).*\[\$ORIGIN\]'; then bad="$bad; $(basename "$f") has no \$ORIGIN RPATH"; fi
    printf '  %-16s %s, needs: %s\n' "$(basename "$f")" "$g" "$(needed "$f" | tr '\n' ' ')"
}
SYS='libc\.so\.6|libm\.so\.6|ld-linux-x86-64\.so\.2|libdl\.so\.2|libpthread\.so\.0|libz\.so\.1'
echo "package_linux: $VER -- what each program needs from the system:"
check "$OUT/native_play" "$SYS" 0
check "$OUT/libb3render.so" "$SYS|libSDL3\.so\.0" 1
check "$OUT/b3launcher" "$SYS|libSDL3\.so\.0" 1
check "$OUT/b3modtool" "$SYS" 0
check "$SDLLIB" 'libc\.so\.6|libm\.so\.6|ld-linux-x86-64\.so\.2' 0
SUFFIX=""
if [ -n "$bad" ]; then
    [ "${NONPORTABLE:-0}" = 1 ] || die "not portable${bad} (a LINUX_RELEASE=1 build with tools/release/build_sdl3_linux.sh's SDL3; NONPORTABLE=1: for this machine only)"
    echo "package_linux: WARNING: not portable${bad}: packaged as -nonportable (this machine only)" >&2
    SUFFIX=-nonportable
fi
NAME=burnout3-native-$(echo "$VER" | tr -c 'A-Za-z0-9._\n-' '-')-linux-x86_64$SUFFIX
echo "package_linux: $NAME"

# ---- stage ----
ST=$REL/stage/$NAME
DBG=$REL/debug/$NAME
rm -rf "$REL/stage" "$DBG"
mkdir -p "$ST/LICENSES" "$DBG"
for f in "${PROGS[@]}"; do
    objcopy --only-keep-debug "$OUT/$f" "$DBG/$f.debug"
    strip --strip-unneeded -o "$ST/$f" "$OUT/$f"
    objcopy --add-gnu-debuglink="$DBG/$f.debug" "$ST/$f"
done
chmod 755 "$ST/b3launcher" "$ST/native_play" "$ST/b3modtool"
chmod 644 "$ST/libb3render.so"
cp -L "$SDLLIB" "$ST/libSDL3.so.0"; chmod 644 "$ST/libSDL3.so.0"
cp "$PORT/assets/b3-icon.png" "$ST/"
python3 -I "$PORT/tools/release/make_icons.py" --check > /dev/null || die "the icon files are out of date: run tools/release/make_icons.py"
mkdir -p "$ST/icons" && cp -r "$PORT/assets/icon/hicolor" "$ST/icons/"   # the hicolor tree; add-to-menu.sh installs it
find "$ST/icons" -type d -exec chmod 755 {} + && find "$ST/icons" -type f -exec chmod 644 {} +
python3 -I "$PORT/tools/blender/make_addon_zip.py" "$ST/b3_car_tools.zip" > /dev/null || die "make_addon_zip failed"   # blender1
chmod 644 "$ST/b3_car_tools.zip"
install -m 644 "$HERE/linux/burnout3-native.desktop" "$ST/burnout3-native.desktop"
chmod 755 "$ST/burnout3-native.desktop"   # a launcher file managers may start (after "Allow Launching")
install -m 755 "$HERE/linux/add-to-menu.sh" "$ST/add-to-menu.sh"

# the texts: @VERSION@ / @SDL_VERSION@ filled in
SDLV=$(sed -n 's/^Version: *//p' "$SDL3_DIR"/lib/pkgconfig/sdl3.pc "$SDL3_DIR"/lib/x86_64-linux-gnu/pkgconfig/sdl3.pc 2>/dev/null | head -1 || true)
text() { sed -e "s/@VERSION@/$VER/g" -e "s/@SDL_VERSION@/${SDLV:-3}/g" "$1" > "$2"; }
text "$HERE/README-TESTERS-linux.txt" "$ST/README-TESTERS.txt"
text "$HERE/KNOWN-ISSUES.txt" "$ST/KNOWN-ISSUES.txt"
grep -q '^- Mac and Linux builds (Windows only for now)\.$' "$ST/KNOWN-ISSUES.txt" \
    || die "KNOWN-ISSUES.txt: its 'Not in this build' platform line changed: update package_linux.sh"
sed -i 's/^- Mac and Linux builds (Windows only for now)\.$/- A Mac build (Windows and Linux for now)./' "$ST/KNOWN-ISSUES.txt"
text "$HERE/THIRD-PARTY-NOTICES-linux.txt" "$ST/LICENSES/THIRD-PARTY-NOTICES.txt"
printf 'Burnout 3 native port %s\ntest build for Linux (x86-64, glibc 2.35 or newer), packaged %s\n' "$VER" "$(date -u +%Y-%m-%d)" > "$ST/VERSION.txt"
[ -n "$SUFFIX" ] && printf 'NOT PORTABLE: for checks on the build machine only (%s)\n' "${bad#; }" >> "$ST/VERSION.txt"
lic() { [ -f "$1" ] || die "no license file $1"; install -m 644 "$1" "$ST/LICENSES/$2"; }
SDLLIC=""
for c in "$SDL3_DIR/share/licenses/SDL3/LICENSE.txt" "$SDL3_DIR/share/doc/libsdl3-0/copyright" "$OUT/sdl3/src/LICENSE.txt"; do
    [ -f "$c" ] && { SDLLIC=$c; break; }
done
lic "${SDLLIC:-$SDL3_DIR/share/licenses/SDL3/LICENSE.txt}" SDL3-LICENSE.txt
awk '/This software is available under 2 licenses/{on=1} on{print} /^\*\//{if(on) exit}' "$PORT/src/b3launcher/third_party/stb_truetype.h" \
    > "$ST/LICENSES/stb_truetype-LICENSE.txt"
[ -s "$ST/LICENSES/stb_truetype-LICENSE.txt" ] || die "no license block in stb_truetype.h"
lic "$PORT/src/native/third_party/LICENSE-dr_libs.txt" dr_libs-LICENSE.txt
lic "$PORT/src/native/third_party/LICENSE-minimp3.txt" minimp3-LICENSE.txt
lic "$PORT/src/native/third_party/LICENSE-stb_vorbis.txt" stb_vorbis-LICENSE.txt
lic "$PORT/src/third_party/lua-5.4.9/LICENSE.txt" Lua-LICENSE.txt   # lua6: script mods (src/native/script_mods.h)
# chd1: .chd disc images (src/native/third_party/libchdr: libchdr and the LZMA SDK, miniz and zstd decoders in it)
lic "$PORT/src/native/third_party/libchdr/LICENSE-libchdr.txt" libchdr-LICENSE.txt
lic "$PORT/src/native/third_party/libchdr/LICENSE-lzma.txt" lzma-LICENSE.txt
lic "$PORT/src/native/third_party/libchdr/LICENSE-miniz.txt" miniz-LICENSE.txt
lic "$PORT/src/native/third_party/libchdr/LICENSE-zstd.txt" zstd-LICENSE.txt
lic "$HERE/licenses/GCC-RUNTIME-LIBRARY-EXCEPTION.txt" GCC-RUNTIME-LIBRARY-EXCEPTION.txt

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

# ---- the tarball (files owned by root:root in the archive, sorted, no build machine's user names) ----
TGZ=$REL/$NAME.tar.gz
tar -C "$REL/stage" --sort=name --owner=0 --group=0 --numeric-owner -czf "$TGZ.tmp" "$NAME"
mv "$TGZ.tmp" "$TGZ"
gzip -t "$TGZ"
echo "package_linux: $TGZ ($(du -h "$TGZ" | cut -f1)); symbols: $DBG"
tar -tzvf "$TGZ" | awk '{printf "  %10s  %s %s\n", $3, $1, $6}'

# ---- SMOKE=1: the package from its own folder ----
[ "${SMOKE:-0}" = 1 ] || exit 0
SM=$REL/smoke
rm -rf "$SM"; mkdir -p "$SM"
tar -xzf "$TGZ" -C "$SM"
P=$SM/$NAME
echo "package_linux: smoke test in $P"
# the launcher: its screenshot mode (offscreen; never START GAME), a scratch user data folder
mkdir -p "$SM/launcher-pref" "$SM/launcher-shots"
(cd "$SM" && env -u B3LAUNCHER_SHOT_START LD_DEBUG=libs LD_DEBUG_OUTPUT="$SM/ld-launcher" \
    timeout 300 "$P/b3launcher" --screenshot "$SM/launcher-shots" --pref "$SM/launcher-pref") \
    > "$SM/launcher.log" 2>&1 || die "b3launcher --screenshot failed (log $SM/launcher.log)"
n=$(find "$SM/launcher-shots" -name '*.png' | wc -l)
[ "$n" -gt 0 ] || die "b3launcher drew no pages ($SM/launcher.log)"
# (the dynamic loader's own record: which file each library came from)
loaded() { grep -h "calling init: .*/$1" "$SM/$2".* 2>/dev/null | sed -n 's/.*calling init: \([^ ]*\).*/\1/p' | sort -u | tr '\n' ' '; }
L=$(loaded libSDL3 ld-launcher)
[ "$L" = "$P/libSDL3.so.0 " ] || die "b3launcher loaded libSDL3 from ${L:-nowhere} (not $P)"
echo "package_linux: b3launcher --screenshot: $n pages drawn ($SM/launcher-shots); libSDL3: $L"
# add-to-menu.sh into a scratch XDG data folder
XDG_DATA_HOME=$SM/xdg "$P/add-to-menu.sh" > /dev/null
[ -s "$SM/xdg/icons/hicolor/256x256/apps/burnout3-native.png" ] || die "add-to-menu.sh installed no icons"
grep -q '^Icon=burnout3-native$' "$SM/xdg/applications/b3port.launcher.desktop" || die "add-to-menu.sh wrote a wrong Icon="
grep -q "^Exec=\"$P/b3launcher\"$" "$SM/xdg/applications/b3port.launcher.desktop" || die "add-to-menu.sh wrote a wrong entry"
XDG_DATA_HOME=$SM/xdg "$P/add-to-menu.sh" --remove > /dev/null
echo "package_linux: add-to-menu.sh: entry written and removed (scratch XDG_DATA_HOME)"
# the game: the race check with the packaged native_play (its libb3render.so and libSDL3.so.0 from the folder)
LD_DEBUG=libs LD_DEBUG_OUTPUT="$SM/ld-race" "$PORT/tools/native/race_check.sh" "$P/native_play" "$SM/race_check" \
    | tail -1 | tee "$SM/race_check.verdict"
grep -q '^race_check: PASS' "$SM/race_check.verdict" || die "race check failed (log $SM/race_check/native_play.log)"
R=$(loaded libb3render ld-race); S=$(loaded libSDL3 ld-race)
[ "$R" = "$P/libb3render.so " ] && [ "$S" = "$P/libSDL3.so.0 " ] \
    || die "native_play loaded libb3render from ${R:-nowhere}, libSDL3 from ${S:-nowhere} (not $P)"
echo "package_linux: native_play loaded $R$S"
rm -f "$SM"/ld-launcher.* "$SM"/ld-race.*
echo "package_linux: smoke test PASS"
