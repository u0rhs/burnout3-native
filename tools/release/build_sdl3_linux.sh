#!/bin/bash
# build_sdl3_linux.sh -- a portable libSDL3.so.0 for the Linux tester package (tools/release/package_linux.sh):
#   tools/release/build_sdl3_linux.sh [work dir]      default build/linux-release/sdl3; prints the install prefix last
# Ubuntu's libsdl3-0 (what `make render` uses, ../ext/sdl3-3.4.2) needs glibc 2.43 and links X11, Wayland, PulseAudio,
# libdecor, fribidi, gbm ... directly, so it starts only where all of them are installed. This one is SDL's own
# release (3.4.2, the version the port is built against) built the way SDL's binary releases are: every backend's
# library loaded at run time (dlopen), so libSDL3.so.0 needs only libc / libm and uses whatever the system has
# (X11 or Wayland; PipeWire, PulseAudio or ALSA; Vulkan through the system's loader), and glibc symbols no newer than
# 2.35 (include/b3compat/glibc_compat.h force-included, as the port's own Linux release build; SDL's own strlcpy /
# strlcat / wcslcpy / wcslcat, as glibc has them only from 2.38).
# The headers it compiles against come from this machine's apt (Ubuntu 26.04) -dev packages, downloaded and unpacked
# under the work dir without root (apt-get download checks them against the signed package index); cmake and pkgconf
# the same way when the machine has none. The source tarball is checked against its sha256 (GitHub's published
# digest for the release asset). Off: KMSDRM, libusb, JACK, sndio, liburing, fribidi / libthai, ibus.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
PORT=$(cd "$HERE/../.." && pwd)
W=$(mkdir -p "${1:-$PORT/build/linux-release/sdl3}" && cd "${1:-$PORT/build/linux-release/sdl3}" && pwd)
SDL_VER=3.4.2
SDL_SHA256=ef39a2e3f9a8a78296c40da701967dd1b0d0d6e267e483863ce70f8a03b4050c
die() { echo "build_sdl3_linux: $*" >&2; exit 1; }
PREFIX=$W/install
BUILT="$SDL_VER $SDL_SHA256 $(sha256sum < "$PORT/include/b3compat/glibc_compat.h" | cut -c1-16)"
if [ -f "$PREFIX/lib/libSDL3.so.0" ] && [ "$(cat "$PREFIX/.built" 2>/dev/null)" = "$BUILT" ]; then
    echo "$PREFIX"; exit 0
fi

# 1. the source
mkdir -p "$W/dl"
TGZ=$W/dl/SDL3-$SDL_VER.tar.gz
if [ ! -f "$TGZ" ]; then
    curl -sSLf -o "$TGZ.part" "https://github.com/libsdl-org/SDL/releases/download/release-$SDL_VER/SDL3-$SDL_VER.tar.gz" \
        || die "download of SDL3-$SDL_VER.tar.gz failed"
    mv "$TGZ.part" "$TGZ"
fi
echo "$SDL_SHA256  $TGZ" | sha256sum --quiet -c - || die "SDL3-$SDL_VER.tar.gz does not match its sha256"
rm -rf "$W/src"; mkdir -p "$W/src"
tar -xzf "$TGZ" -C "$W/src" --strip-components=1

# 2. headers: the -dev packages (+ the runtime libraries their .so links point at: SDL reads the SONAMEs it will
#    load) unpacked into root/ (never on any program's library path); cmake / pkgconf into tools/ when missing
PKGS=(libx11-dev libx11-6 libxext-dev libxext6 libxrandr-dev libxrandr2 libxcursor-dev libxcursor1 libxi-dev libxi6
      libxfixes-dev libxfixes3 libxss-dev libxss1 libxtst-dev libxtst6 libxrender-dev libxrender1 x11proto-dev
      libxcb1-dev libxcb1 libxau-dev libxdmcp-dev xtrans-dev
      libwayland-dev libwayland-client0 libwayland-cursor0 libwayland-egl1 libwayland-bin libxkbcommon-dev
      libxkbcommon0 libdecor-0-dev libdecor-0-0 libegl-dev libegl1 libgl-dev
      libasound2-dev libasound2t64 libpulse-dev libpulse0 libpipewire-0.3-dev libpipewire-0.3-0t64 libspa-0.2-dev
      libdbus-1-dev libdbus-1-3 libudev-dev libudev1 libffi-dev libffi8)
TPKGS=()
command -v pkg-config > /dev/null || TPKGS+=(pkgconf pkgconf-bin libpkgconf7)
command -v cmake > /dev/null || TPKGS+=(cmake cmake-data libjsoncpp26 librhash1 libuv1t64 libarchive13t64)
fetch() {   # fetch <dir> <packages...>: apt-get download (again only when the list changed)
    local d=$1; shift
    mkdir -p "$d"
    if [ "$(cat "$d/.list" 2>/dev/null)" != "$*" ]; then
        rm -f "$d/"*.deb
        if [ $# -gt 0 ]; then (cd "$d" && apt-get download "$@" > /dev/null) || die "apt-get download failed"; fi
        printf '%s' "$*" > "$d/.list"
    fi
}
fetch "$W/debs" "${PKGS[@]}"
fetch "$W/tooldebs" ${TPKGS[@]+"${TPKGS[@]}"}
ROOT=$W/root
T=$W/tools
rm -rf "$ROOT" "$T"; mkdir -p "$ROOT" "$T"
for d in "$W/debs/"*.deb; do dpkg-deb -x "$d" "$ROOT"; done
for d in "$W/tooldebs/"*.deb; do [ -f "$d" ] && dpkg-deb -x "$d" "$T"; done
export PATH=$T/usr/bin:$ROOT/usr/bin:$PATH   # (+ wayland-scanner from libwayland-bin)
CMAKE=$(command -v cmake)
TOOLENV=(env)
[ -x "$T/usr/bin/cmake" ] && TOOLENV=(env LD_LIBRARY_PATH="$T/usr/lib/x86_64-linux-gnu" CMAKE_ROOT="$(ls -d "$T"/usr/share/cmake-* | head -1)")
# pkg-config inside the unpacked tree (its .pc files say prefix=/usr)
export PKG_CONFIG_SYSROOT_DIR=$ROOT
export PKG_CONFIG_LIBDIR=$ROOT/usr/lib/x86_64-linux-gnu/pkgconfig:$ROOT/usr/share/pkgconfig
PKGCONF=$(command -v pkg-config || command -v pkgconf)

# 3. configure and build (Release, shared, dynamic loading for every backend: SDL's defaults, made explicit)
COMPAT="-include $PORT/include/b3compat/glibc_compat.h -D_GNU_SOURCE=1"
rm -rf "$W/build" "$PREFIX"
"${TOOLENV[@]}" "$CMAKE" -S "$W/src" -B "$W/build" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PREFIX" \
    -DCMAKE_INSTALL_LIBDIR=lib -DPKG_CONFIG_EXECUTABLE="$PKGCONF" -DCMAKE_PREFIX_PATH="$ROOT/usr" \
    -DCMAKE_C_FLAGS="$COMPAT -I$ROOT/usr/include" -DCMAKE_CXX_FLAGS="$COMPAT -I$ROOT/usr/include" \
    -DCMAKE_SHARED_LINKER_FLAGS="-Wl,--as-needed" \
    -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TEST_LIBRARY=OFF -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF -DSDL_INSTALL_DOCS=OFF \
    -DSDL_DEPS_SHARED=ON -DSDL_X11_SHARED=ON -DSDL_WAYLAND_SHARED=ON -DSDL_WAYLAND_LIBDECOR_SHARED=ON \
    -DSDL_PULSEAUDIO_SHARED=ON -DSDL_PIPEWIRE_SHARED=ON -DSDL_ALSA_SHARED=ON \
    -DSDL_KMSDRM=OFF -DSDL_HIDAPI_LIBUSB=OFF -DSDL_JACK=OFF -DSDL_SNDIO=OFF -DSDL_LIBURING=OFF \
    -DSDL_FRIBIDI=OFF -DSDL_LIBTHAI=OFF -DSDL_IBUS=OFF \
    -DLIBC_HAS_STRLCPY= -DLIBC_HAS_STRLCAT= -DLIBC_HAS_WCSLCPY= -DLIBC_HAS_WCSLCAT= \
    > "$W/configure.log" 2>&1 || { tail -40 "$W/configure.log" >&2; die "cmake configure failed ($W/configure.log)"; }
"${TOOLENV[@]}" "$CMAKE" --build "$W/build" -j "${JOBS:-8}" > "$W/build.log" 2>&1 \
    || { tail -40 "$W/build.log" >&2; die "build failed ($W/build.log)"; }
"${TOOLENV[@]}" "$CMAKE" --install "$W/build" > /dev/null
LIB=$PREFIX/lib/libSDL3.so.0
[ -f "$LIB" ] || die "no $LIB"

# 4. checks: only libc / libm (+ ld.so) needed, no glibc symbol newer than 2.35, X11 + Wayland + PipeWire /
#    PulseAudio / ALSA + Vulkan in
NEED=$(readelf -d "$LIB" | sed -n 's/.*(NEEDED).*\[\(.*\)\]/\1/p' | grep -vxE 'libc\.so\.6|libm\.so\.6|ld-linux-x86-64\.so\.2' || true)
[ -z "$NEED" ] || die "libSDL3.so.0 links more than libc / libm: $NEED"
GV=$(objdump -T "$LIB" | grep -o 'GLIBC_[0-9.]*' | sort -uV | tail -1)
[ "$(printf '%s\n' "$GV" GLIBC_2.35 | sort -V | tail -1)" = GLIBC_2.35 ] || die "libSDL3.so.0 needs $GV (more than 2.35)"
CFG=$(find "$W/build" -name SDL_build_config.h | head -1)
[ -n "$CFG" ] || die "no SDL_build_config.h in $W/build"
for want in SDL_VIDEO_DRIVER_X11 SDL_VIDEO_DRIVER_WAYLAND SDL_AUDIO_DRIVER_PIPEWIRE SDL_AUDIO_DRIVER_PULSEAUDIO \
            SDL_AUDIO_DRIVER_ALSA SDL_VIDEO_VULKAN SDL_GPU_VULKAN; do
    grep -qE "^#define $want( |$)" "$CFG" || die "built without $want ($CFG)"
done
echo "$BUILT" > "$PREFIX/.built"
echo "build_sdl3_linux: $LIB ($GV; dynamic: X11, Wayland, PipeWire, PulseAudio, ALSA, Vulkan)" >&2
echo "$PREFIX"
