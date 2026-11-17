# local_env.sh -- sourced by the build scripts in tools/: the machine's own paths from a git-ignored `local.env`
# (the repo root's, else -- in a git worktree -- the main checkout's), the same file the Makefile includes. Its lines are
# `NAME ?= value` (or `NAME = value`); a variable already set in the environment wins. local.env.example lists the
# names; docs/BUILDING.md says what each is for. Usage (bash): . "$PORT/tools/local_env.sh"; b3_local_env "$PORT"
b3_local_env() {
    local dir=${1:-.} root common f line k v
    root=$(git -C "$dir" rev-parse --show-toplevel 2>/dev/null) || root=$dir
    common=$(git -C "$dir" rev-parse --path-format=absolute --git-common-dir 2>/dev/null) || common=
    for f in "$root/local.env" ${common:+"$(dirname "$common")/local.env"}; do
        [ -f "$f" ] || continue
        while IFS= read -r line || [ -n "$line" ]; do
            case $line in '' | '#'*) continue ;; esac
            case $line in *=*) ;; *) continue ;; esac
            k=${line%%=*}
            v=${line#*=}
            k=${k//[[:space:]]/}
            k=${k%[?:+]}
            v=${v#"${v%%[![:space:]]*}"}
            v=${v%"${v##*[![:space:]]}"}
            [[ $k =~ ^[A-Za-z_][A-Za-z0-9_]*$ ]] || continue
            [ -n "${!k+x}" ] || export "$k=$v"
        done < "$f"
        return 0
    done
    return 0
}
