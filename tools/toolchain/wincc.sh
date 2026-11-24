# wincc.sh -- sourced by the Windows build scripts (tools/native/build_win.sh, tools/render/build_win.sh): the verified
# MSYS2 toolchain (toolchain.sha256) run from WSL, with
#   - wincc_stage: the tree staged by content (a file is rewritten only when its bytes changed, so an unchanged file keeps
#     its time and the scripts' "object newer than its source / no header newer than the last run" checks skip it; a
#     fresh checkout or worktree no longer looks all new);
#   - ccache (1.3.1, when WSL has it and CCACHE is not 0): a compile runs as `ccache <toolchain>/bin/g++.exe @<rsp>`,
#     the Windows program started directly through WSL interop (PATH passed as a Windows list: WSLENV PATH/l) by a small
#     wrapper that rewrites its -MF list with LF line ends (the compiler writes CRLF, which ccache cannot read). Depend
#     mode (CCACHE_DEPEND: the compiler's -MMD list, no preprocessor run; every path relative to the stage, as the rsp's
#     -I, source, -o and -MF are) keyed on the toolchain's checksum list, in its own cache folder ($B3_WIN_CCACHE_DIR,
#     default ~/.cache/ccache-mingw, $B3_WIN_CCACHE_SIZE default 20G), apart from the Linux build's. A hit is the object
#     the same compile made before, so a respin into a new OUT folder recompiles only what changed.
# Without ccache (or CCACHE=0) a compile goes through cmd.exe as before (tc.cmd sets the toolchain's PATH).

# wincc_init <toolchain mingw64 dir> <port root> <folder for tc.cmd>
wincc_init() {
    WINCC_TC=$1
    WINCC_CMD=$3/tc.cmd
    local w
    w=$(wslpath -w "$WINCC_TC")
    printf '@echo off\r\nset "PATH=%s\\bin;%%SystemRoot%%\\system32;%%SystemRoot%%"\r\n"%s\\bin\\%%1" @%%2\r\n' "$w" "$w" > "$WINCC_CMD"
    WINCC_CCACHE=0
    if [ "${CCACHE:-1}" != 0 ] && command -v ccache > /dev/null 2>&1; then
        WINCC_CCACHE=1
        WINCC_CCACHE_DIR=${B3_WIN_CCACHE_DIR:-$HOME/.cache/ccache-mingw}
        WINCC_CHECK="string:$(sha256sum < "$2/tools/toolchain/toolchain.sha256" | cut -c1-32)"
        mkdir -p "$WINCC_CCACHE_DIR" "$3/wincc-bin"
        # ccache runs these (named as MinGW's, so it takes them for GCC): the Windows compiler, then its -MF list with LF
        # line ends (it writes CRLF, which ccache's depend mode cannot read: every lookup would miss)
        local t
        for t in g++ gcc; do
            printf '%s\n' '#!/bin/sh' "\"$WINCC_TC/bin/$t.exe\" \"\$@\"" 'rc=$?' 'mf=; prev=' \
                'for a in "$@"; do [ "$prev" = -MF ] && mf=$a; prev=$a; done' \
                '[ "$rc" = 0 ] && [ -n "$mf" ] && [ -f "$mf" ] && sed -i "s/\r\$//" "$mf"' 'exit "$rc"' > "$3/wincc-bin/x86_64-w64-mingw32-$t"
            chmod +x "$3/wincc-bin/x86_64-w64-mingw32-$t"
        done
        WINCC_BIN=$3/wincc-bin
        [ -f "$WINCC_CCACHE_DIR/ccache.conf" ] || printf 'max_size = %s\n' "${B3_WIN_CCACHE_SIZE:-20G}" > "$WINCC_CCACHE_DIR/ccache.conf"
        echo "wincc: ccache for the MinGW compiler ($WINCC_CCACHE_DIR; CCACHE=0: none)" >&2
    else
        echo "wincc: no ccache (CCACHE=${CCACHE:-1}, ccache $(command -v ccache > /dev/null 2>&1 && echo found || echo 'not installed')): every compile runs" >&2
    fi
}

# wincc_run <cwd> <tool: g++.exe gcc.exe windres.exe ...> <rsp, relative to cwd>: one run (WSL interop's start-up
# failures retried, as before); its output on stdout / stderr
wincc_run() {
    local cwd=$1 tool=$2 rsp=$3 try rc out
    for try in 1 2 3 4 5 6; do
        rc=0
        if [ "$WINCC_CCACHE" = 1 ] && { [ "$tool" = g++.exe ] || [ "$tool" = gcc.exe ]; } && grep -qx -- '-c' "$cwd/$rsp"; then
            out=$( (cd "$cwd" && PATH="$WINCC_TC/bin:$PATH" WSLENV="PATH/l${WSLENV:+:$WSLENV}" CCACHE_DIR="$WINCC_CCACHE_DIR" \
                    CCACHE_DEPEND=1 CCACHE_NOHASHDIR=1 CCACHE_COMPILERCHECK="$WINCC_CHECK" CCACHE_BASEDIR= CCACHE_PREFIX= \
                    ccache "$WINCC_BIN/x86_64-w64-mingw32-${tool%.exe}" "@$rsp" < /dev/null) 2>&1 ) || rc=$?
        else
            out=$( (cd "$cwd" && cmd.exe /c "$(wslpath -w "$WINCC_CMD")" "$tool" "$(wslpath -w "$cwd/$rsp")" < /dev/null) 2>&1 ) || rc=$?
        fi
        if [ "$rc" -eq 0 ] || ! grep -q 'UtilAcceptVsock\|WSL .*ERROR' <<< "$out"; then break; fi
        sleep 2
    done
    if [ -n "$out" ]; then printf '%s\n' "$out"; fi
    return "$rc"
}

# wincc_stage <source root> <stage root> <relative path>...: each dir (or file) mirrored into the stage by content: new
# and changed files written, files gone from a source dir deleted, the rest untouched. The stage's own record
# (.wincc_stage: each staged file's size, time and SHA-1) saves reading the stage again over /mnt/c; a file whose size or
# time differs from it is compared byte by byte. Prints the number of files written.
wincc_stage() {   # (retried: /mnt/c can fail a call with ENOMEM under load; a run again is the same copy)
    local try
    for try in 1 2 3; do
        _wincc_stage "$@" && return 0
        echo "wincc_stage: failed (try $try of 3)" >&2
        sleep 5
    done
    return 1
}
_wincc_stage() {
    python3 -I - "$@" << 'EOF'
import hashlib, json, os, shutil, sys
src, dst, paths = sys.argv[1], sys.argv[2], sys.argv[3:]
rec_path = os.path.join(dst, '.wincc_stage')
try:
    with open(rec_path) as f:
        rec = json.load(f)
except (OSError, ValueError):
    rec = {}
written = 0
def sha(p):
    with open(p, 'rb') as f:
        return hashlib.sha1(f.read()).hexdigest()
def stage(a, b, key):
    global written
    h = sha(a)
    try:
        st = os.stat(b)
        r = rec.get(key)
        if r and r[0] == st.st_size and r[1] == st.st_mtime_ns and r[2] == h:
            return
        if st.st_size == os.path.getsize(a) and os.path.isfile(b) and sha(b) == h:
            rec[key] = [st.st_size, st.st_mtime_ns, h]
            return
        if not os.path.isfile(b):
            shutil.rmtree(b)
    except FileNotFoundError:
        pass
    os.makedirs(os.path.dirname(b) or '.', exist_ok=True)
    shutil.copyfile(a, b)
    st = os.stat(b)
    rec[key] = [st.st_size, st.st_mtime_ns, h]
    written += 1
for d in paths:
    s, t = os.path.join(src, d), os.path.join(dst, d)
    if os.path.isfile(s):
        stage(s, t, os.path.normpath(d))
        continue
    want = set()
    for root, subdirs, files in os.walk(s):
        rel = os.path.relpath(root, s)
        for n in files:
            r = os.path.normpath(os.path.join(d, rel, n))
            want.add(r)
            stage(os.path.join(root, n), os.path.join(dst, r), r)
    for root, subdirs, files in os.walk(t, topdown=False):
        for n in files:
            r = os.path.normpath(os.path.relpath(os.path.join(root, n), dst))
            if r not in want:
                os.remove(os.path.join(root, n))
                rec.pop(r, None)
        if root != t and not os.listdir(root):
            os.rmdir(root)
os.makedirs(dst, exist_ok=True)
with open(rec_path + '.new', 'w') as f:
    json.dump(rec, f)
os.replace(rec_path + '.new', rec_path)
print(written)
EOF
}
