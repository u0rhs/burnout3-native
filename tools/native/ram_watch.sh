#!/bin/bash
# ram_watch.sh -- native_play under gdb with tools/native/ram_watch.py: who writes a guest RAM word (lane-noise).
# Give it to race_check.sh as the native_play, so the scenario's keys and environment stay as they are:
#
#   RAM_WATCH_NP=build/native_play B3NATIVE_GDB_WATCH=20000:64D3EC RAM_WATCH_VALUE=45D22000 \
#     RAM_WATCH_OUT=$PWD/build/watch.log VBLANKS=20810 tools/native/race_check.sh tools/native/ram_watch.sh build/rc_watch
#
# (race_check then fails on the missing race shots when VBLANKS ends the run early: expected.) The settings are in
# ram_watch.py's header. gdb turns ASLR off, so host addresses left in guest RAM read 0x0000555x / 0x00007FFx there.
D=$(cd "$(dirname "$0")" && pwd)
NP=${RAM_WATCH_NP:-$D/../../build/native_play}
exec gdb -q -batch -x "$D/ram_watch.py" --args "$NP" "$@"
