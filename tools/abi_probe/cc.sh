#!/bin/bash
# cc.sh <probe.cpp>: compile an ABI probe with the decomp's MWCC (3.0.1b119p, its project flags, under wibo) and
# disassemble it next to the source as <probe>.dis.txt. The compiler and the licence stay outside this repo (path
# references, as in the decomp's tools/mwcc_flags.sh); the .o goes to $TMPDIR. Probes are our own code.
set -e
src=$(readlink -f "$1"); base="${src%.cpp}"; obj="${TMPDIR:-/tmp}/$(basename "$base").o"
: "${MWCC_DIR:?set MWCC_DIR to the MWCC 3.0.1b119p folder}" "${LM_LICENSE_FILE:?set LM_LICENSE_FILE to its licence file}" "${WIBO:?set WIBO to the wibo binary}"
cd "$MWCC_DIR"
MWCIncludes="$PWD" \
  "$WIBO" "$PWD/mwccps2.exe" -O4 -str readonly -Cpp_exceptions off -inline level=8 -c "$src" -o "$obj"
mips-linux-gnu-objdump -d -r "$obj" | sed "s#$obj#$(basename "$base").o#" > "$base.dis.txt"
