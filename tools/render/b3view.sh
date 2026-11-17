#!/bin/bash
# Runs build/b3view (B3BIN=b3replay: build/b3replay) on Linux/WSL with the unpacked SDL3 3.4.2 libraries (../ext/sdl3-3.4.2; libpulse's own RUNPATH
# hides its private libpulsecommon from the executable's rpath, hence LD_LIBRARY_PATH). Arguments go to b3view.
WS=${B3_WORKSPACE:-$(dirname "$(dirname "$(git -C "$(dirname "$0")" rev-parse --path-format=absolute --git-common-dir)")")}   # the folder that holds this repo (its main checkout)
HERE=$(cd "$(dirname "$0")" && pwd)
L=${SDL3_LIBDIR:-$WS/ext/sdl3-3.4.2/usr/lib/x86_64-linux-gnu}
export LD_LIBRARY_PATH="$L:$L/pulseaudio${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
exec "$HERE/../../build/${B3BIN:-b3view}" "$@"
