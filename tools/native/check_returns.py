#!/usr/bin/env python3
"""genret: no generated non-void function may fall off its end.

Generated units are compiled with -w (the drafts' warnings are noise), which also hides -Wreturn-type: a draft written
from retail code where MWCC simply leaves v0 as it was compiles to undefined behaviour in C++ (GCC ran ForAllGames
straight into the next block: the online JOIN GAME page broke). Hand-written code is built with -Werror=return-type
already; the generated units only change when they are regenerated, so the generation targets run this check (the
native generation, gen/decomp's generation, native-bundle before the bundle is synced; `make check-returns` by hand).

Each unit is compiled with the given compiler and flags minus -w, plus -Wreturn-type -Werror=return-type -O0 -g0 -S
-o /dev/null. The trailing -O0 overrides the build's -O2: GCC reports the warning from an early pass that runs before
any optimisation (and clang from its front end), so the level does not change the sites, and -O0 compiles fastest;
-fsyntax-only would not report GCC's. GCC's and clang's wordings are both recognised.

Results are cached per unit under --cache, keyed by the unit's bytes, the bytes of every file under each --headers
directory (the generated headers: their inline functions are checked too, so a header change rechecks every unit), the
flags and the compiler's version; a rerun compiles only changed units. --all ignores the cache.

Parallelism: inside make (MAKEFLAGS has a jobserver) each compile beyond the first takes a job token from make's
jobserver, so `make -jN` stays at N jobs in all; --jobs (default: N from MAKEFLAGS' -jN, 1 under make without one, 8
outside make; 1 under make -jN when the jobserver cannot be reached) caps the threads; a non-blocking jobserver
pipe (older make) is waited on.

  check_returns.py --cache build/check_returns/native [--headers DIR]... [--jobs N] [--all] UNITS_DIR... -- g++ <flags>
  check_returns.py --selftest --cache DIR -- g++ <flags>     (a fixture that falls off its end must be caught)

Exit 1 listing every fall-off site; 0 when there is none."""
import argparse
import concurrent.futures
import hashlib
import os
import re
import select
import shutil
import subprocess
import sys
import threading

# a warning, or an error under -Werror=return-type. GCC: "control reaches end of non-void function", "no return
# statement in function returning non-void"; clang: "non-void function does not return a value [in all control
# paths]", "control may reach end of non-void function", the same for lambdas / coroutines.
WARN = re.compile(r"^(?P<loc>[^\s:][^:]*:\d+:\d+): (?:warning|error): (?P<msg>control reaches end of non-void function|"
                  r"no return statement in function returning non-void|"
                  r"non-void (?:function|lambda|coroutine) does not return a value(?: in all control paths)?|"
                  r"control may reach end of non-void (?:function|lambda|coroutine))")


def compiler_id(cmd):
    try:
        out = subprocess.run(cmd + ["--version"], capture_output=True, text=True).stdout
    except OSError as e:
        sys.exit("check_returns: cannot run %s: %s" % (cmd[0], e))
    return out.splitlines()[0] if out else cmd[0]


class Tokens:
    """make's jobserver (GNU make 4.4 fifo, or the older R,W pipe pair when the recipe is marked +): one implicit token
    is ours; every further concurrent compile reads a byte from the jobserver and writes it back after."""

    def __init__(self):
        self.lock = threading.Lock()
        self.implicit = True
        self.rfd = self.wfd = None
        m = re.search(r"--jobserver-auth=(\S+)", os.environ.get("MAKEFLAGS", ""))
        if not m:
            return
        auth = m.group(1)
        try:
            if auth.startswith("fifo:"):
                self.rfd = self.wfd = os.open(auth[5:], os.O_RDWR)
            else:
                r, w = (int(x) for x in auth.split(","))
                os.fstat(r), os.fstat(w)
                self.rfd, self.wfd = r, w
        except (OSError, ValueError):
            self.rfd = self.wfd = None                   # not passed down (recipe without +): run on the implicit token

    @property
    def shared(self):
        return self.rfd is not None

    def acquire(self):
        with self.lock:
            if self.implicit:
                self.implicit = False
                return None
        while True:
            try:
                tok = os.read(self.rfd, 1)
            except BlockingIOError:
                # an older make hands down a non-blocking read end (shared with its other children): wait for a token
                select.select([self.rfd], [], [])
                continue
            if tok:
                return tok
            raise OSError("check_returns: make's jobserver closed")

    def release(self, tok):
        if tok is None:
            with self.lock:
                self.implicit = True
        else:
            os.write(self.wfd, tok)


def default_jobs(tokens):
    """8 outside make; under make N from -jN when its jobserver is usable (the tokens keep make's total at N), else 1
    (-j without a jobserver we can reach, e.g. a recipe without + under an older make: no way to share make's N)."""
    mf = os.environ.get("MAKEFLAGS")
    if mf is None:
        return 8
    m = re.search(r"(?:^|\s)-j(\d+)", mf)
    return int(m.group(1)) if m and tokens.shared else 1


def check(cmd, path, tokens):
    tok = tokens.acquire() if tokens.shared else None
    try:
        r = subprocess.run(cmd + [path, "-o", os.devnull], capture_output=True, text=True)
    finally:
        if tokens.shared:
            tokens.release(tok)
    hits = []
    for line in r.stderr.splitlines():
        m = WARN.match(line)
        if m:
            hits.append("%s: %s" % (m.group("loc"), m.group("msg")))
    if r.returncode != 0 and not hits:
        # a unit that does not compile at all is the build's business; say so, do not cache it
        return None, (r.stderr.strip().splitlines() or ["exit %d" % r.returncode])[-1]
    return hits, None


def headers_digest(dirs):
    h = hashlib.sha256()
    for d in dirs:
        if not os.path.isdir(d):
            sys.exit("check_returns: no headers directory %s" % d)
        for root, subdirs, files in os.walk(d):
            subdirs.sort()
            for f in sorted(files):
                p = os.path.join(root, f)
                h.update(os.path.relpath(p, d).encode() + b"\0")
                with open(p, "rb") as fh:
                    h.update(fh.read())
                h.update(b"\0")
    return h.hexdigest()


def run(units_dirs, cache_dir, headers, jobs, everything, flags):
    tokens = Tokens()
    if jobs is None:
        jobs = default_jobs(tokens)
    flags = [f for f in flags if f != "-w"]
    # ccache adds nothing for -S to /dev/null; call the compiler itself
    while flags and os.path.basename(flags[0]) == "ccache":
        flags = flags[1:]
    if not flags:
        sys.exit("check_returns: no compiler after --")
    cmd = flags + ["-Wreturn-type", "-Werror=return-type", "-O0", "-g0", "-S"]
    salt = "\0".join([compiler_id(flags[:1]), headers_digest(headers)] + cmd).encode()

    units = []
    for d in units_dirs:
        if not os.path.isdir(d):
            sys.exit("check_returns: no directory %s" % d)
        units += sorted(os.path.join(d, f) for f in os.listdir(d) if f.endswith(".cpp"))

    os.makedirs(cache_dir, exist_ok=True)
    cache_file = os.path.join(cache_dir, "results.tsv")
    cache = {}
    if not everything and os.path.exists(cache_file):
        for line in open(cache_file, encoding="utf-8"):
            key, _, hits = line.rstrip("\n").partition("\t")
            cache[key] = [h for h in hits.split("\t") if h]

    keys, todo = {}, []
    for u in units:
        with open(u, "rb") as f:
            keys[u] = hashlib.sha256(salt + b"\0" + f.read()).hexdigest()
        if keys[u] not in cache:
            todo.append(u)

    failed = []
    with concurrent.futures.ThreadPoolExecutor(max(1, jobs)) as ex:
        for u, (hits, err) in zip(todo, ex.map(lambda u: check(cmd, u, tokens), todo)):
            if err is not None:
                failed.append("%s: %s" % (u, err))
            else:
                cache[keys[u]] = hits

    live = {keys[u] for u in units}
    tmp = cache_file + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        for key in sorted(live & cache.keys()):
            f.write(key + "\t" + "\t".join(cache[key]) + "\n")
    os.replace(tmp, cache_file)

    sites = [h for u in units if keys[u] in cache for h in cache[keys[u]]]
    print("check_returns: %d unit(s), %d compiled now, %d from the cache (%d job(s)%s)" % (
        len(units), len(todo) - len(failed), len(units) - len(todo), max(1, jobs),
        ", make's jobserver" if tokens.shared else ""))
    for f in failed:
        print("check_returns: did not compile (not checked): %s" % f)
    if sites:
        print("check_returns: FAIL %d non-void function(s) fall off their end (undefined behaviour; give the path "
              "retail's value, decomp2host T47_DRAFT_FIXES):" % len(sites))
        for s in sites:
            print("  " + s)
        return 1
    if failed:
        return 1
    print("check_returns: PASS no generated function falls off its end")
    return 0


FIXTURE = """int ok(int p0)
{
    if (p0)
        return 1;
    return 0;
}

int falls_off(int p0)
{
    switch (p0) {
    case 0:
        return 1;
    }
}
"""


def selftest(cache_dir, flags):
    """A two-function unit, one falling off its end, checked with -w in the flags (as the generated units are built):
    must exit 1 naming that one site. Clang's wordings must parse too."""
    for line, want in [("u.cpp:9:1: warning: non-void function does not return a value in all control paths "
                        "[-Wreturn-type]", "non-void function does not return a value in all control paths"),
                       ("u.cpp:9:1: error: control may reach end of non-void function [-Werror,-Wreturn-type]",
                        "control may reach end of non-void function"),
                       ("u.cpp:9:1: error: control reaches end of non-void function [-Werror=return-type]",
                        "control reaches end of non-void function")]:
        m = WARN.match(line)
        if not m or m.group("msg") != want:
            print("check_returns: selftest FAIL parsing %r" % line)
            return 1
    # under make -jN with no jobserver reachable: one job; a non-blocking, empty jobserver pipe is waited on
    saved = os.environ.get("MAKEFLAGS")
    r, w = os.pipe()
    try:
        os.environ["MAKEFLAGS"] = "-j8"
        if default_jobs(Tokens()) != 1:
            print("check_returns: selftest FAIL (make -j8 without a jobserver must give 1 job)")
            return 1
        os.set_blocking(r, False)
        os.environ["MAKEFLAGS"] = "-j8 --jobserver-auth=%d,%d" % (r, w)
        t = Tokens()
        if not t.shared or default_jobs(t) != 8:
            print("check_returns: selftest FAIL (the R,W jobserver was not used)")
            return 1
        t.acquire()                                        # the implicit token
        threading.Timer(0.2, lambda: os.write(w, b"+")).start()
        if t.acquire() != b"+":
            print("check_returns: selftest FAIL (no token from the non-blocking jobserver pipe)")
            return 1
    finally:
        os.close(r)
        os.close(w)
        if saved is None:
            os.environ.pop("MAKEFLAGS", None)
        else:
            os.environ["MAKEFLAGS"] = saved
    units = os.path.join(cache_dir, "units")
    shutil.rmtree(cache_dir, ignore_errors=True)
    os.makedirs(units)
    with open(os.path.join(units, "fall_off.cpp"), "w") as f:
        f.write(FIXTURE)
    import contextlib
    import io
    out = io.StringIO()
    with contextlib.redirect_stdout(out):
        rc = run([units], os.path.join(cache_dir, "cache"), [], 1, True, flags + ["-w"])
    text = out.getvalue()
    shutil.rmtree(cache_dir, ignore_errors=True)
    sites = [l for l in text.splitlines() if l.startswith("  ")]
    if rc != 1 or len(sites) != 1 or "fall_off.cpp:14:" not in sites[0]:
        print("check_returns: selftest FAIL (exit %d, expected 1 with one site at fall_off.cpp:14):\n%s" % (rc, text))
        return 1
    print("check_returns: selftest PASS (the fall-off is caught with -w in the flags: %s)" % sites[0].strip())
    return 0


def main():
    argv = sys.argv[1:]
    if "--" not in argv:
        sys.exit(__doc__)
    k = argv.index("--")
    ap = argparse.ArgumentParser()
    ap.add_argument("--cache", required=True)
    ap.add_argument("--headers", action="append", default=[])
    ap.add_argument("--jobs", type=int, default=None)
    ap.add_argument("--all", action="store_true")
    ap.add_argument("--selftest", action="store_true")
    ap.add_argument("units", nargs="*")
    a = ap.parse_args(argv[:k])
    if a.selftest:
        return selftest(a.cache, argv[k + 1:])
    if not a.units:
        sys.exit("check_returns: no units directory")
    return run(a.units, a.cache, a.headers, a.jobs, a.all, argv[k + 1:])


if __name__ == "__main__":
    sys.exit(main())
