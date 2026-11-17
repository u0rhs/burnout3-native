# ram_watch.py -- gdb script (tools/native/ram_watch.sh): which native code writes a guest RAM word (lane-noise,
# 2026-11-11: how the host-noise writers in guest RAM were found).
#
# native_play's B3NATIVE_GDB_WATCH=<vblank>:<hex>[,<hex>...] calls b3np_gdb_watch(host pointer, guest address) for each
# word at that VBlank; this script breaks there and puts a hardware write watchpoint on the word (x86-64 has four).
# Each later write that changes the word (gdb reports no other) is logged to RAM_WATCH_OUT: the value, then
# RAM_WATCH_DEPTH frames of host backtrace.
#   RAM_WATCH_VALUE  <hex> or <lo>-<hi>: log only writes that leave such a value (default: every write)
#   RAM_WATCH_LIMIT  writes logged at most (default 40; later ones run on unlogged)
#   RAM_WATCH_EXPR   <expr>[;<expr>...], each printed at a logged write; <function substring>|<expr> evaluates it in
#                    the innermost frame whose name contains the substring (e.g. a caller's local)
#   RAM_WATCH_CMDS   a file of gdb commands, one per line, run at each logged write (x/8i $pc, p/x $xmm0.v4_int32 ...)
#   RAM_WATCH_SCAN   <hex>: at each logged write, the guest RAM quads whose fourth lane holds that value
# A guest word written by a DMA shows the platform's memcpy (fromSPR: src/native/dmac_delivery.h spr_dma) -- the
# scratchpad source is madr/sadr/k there; watch the scratchpad address (0x7000xxxx) next.
import gdb, os, struct

want = os.environ.get("RAM_WATCH_VALUE")
if want and "-" in want:
    want = tuple(int(x, 16) for x in want.split("-"))
elif want:
    want = (int(want, 16), int(want, 16))
depth = int(os.environ.get("RAM_WATCH_DEPTH", "8"))
limit = int(os.environ.get("RAM_WATCH_LIMIT", "40"))
out = open(os.environ.get("RAM_WATCH_OUT", "ram_watch.log"), "w")
hits = [0]


def frame_named(sub):
    f = gdb.newest_frame()
    while f is not None and not (f.name() and sub in f.name()):
        f = f.older()
    return f


class Watch(gdb.Breakpoint):
    def __init__(self, host, guest):
        super().__init__("*(unsigned int*)0x%x" % host, gdb.BP_WATCHPOINT, gdb.WP_WRITE, internal=True)
        self.host, self.guest = host, guest

    def stop(self):
        v = int(gdb.parse_and_eval("*(unsigned int*)0x%x" % self.host)) & 0xFFFFFFFF
        if want and not (want[0] <= v <= want[1]):
            return False
        hits[0] += 1
        if hits[0] > limit:
            return False
        out.write("=== guest %08X = %08X\n" % (self.guest, v))
        for e in filter(None, os.environ.get("RAM_WATCH_EXPR", "").split(";")):
            try:
                f = frame_named(e.split("|", 1)[0]) if "|" in e else gdb.newest_frame()
                e = e.split("|", 1)[1] if "|" in e else e
                f.select()
                out.write("  %s = %s\n" % (e, gdb.parse_and_eval(e)))
            except Exception as ex:
                out.write("  %s: (%s)\n" % (e, ex))
        if os.environ.get("RAM_WATCH_CMDS"):
            for c in filter(None, open(os.environ["RAM_WATCH_CMDS"]).read().splitlines()):
                try:
                    gdb.newest_frame().select()
                    out.write(gdb.execute(c, to_string=True))
                except Exception as ex:
                    out.write("  %s: (%s)\n" % (c, ex))
        if os.environ.get("RAM_WATCH_SCAN"):
            sv = int(os.environ["RAM_WATCH_SCAN"], 16)
            ram = int(gdb.parse_and_eval("(long)b3host::detail::g_windows.ram"))
            ws = struct.unpack("<8388608I", bytes(gdb.selected_inferior().read_memory(ram, 0x2000000)))
            found = [i * 4 - 12 for i in range(3, len(ws), 4) if ws[i] == sv]
            out.write("  scan %08X: %d quads: %s\n" % (sv, len(found), " ".join("%08X" % a for a in found[:64])))
        f, k = gdb.newest_frame(), 0
        while f is not None and k < depth:
            sal = f.find_sal()
            out.write("  #%d %s %s:%s\n" % (k, f.name(), sal.symtab.filename if sal.symtab else "?", sal.line))
            f, k = f.older(), k + 1
        out.flush()
        return False


class Entry(gdb.Breakpoint):
    def stop(self):
        host = int(gdb.parse_and_eval("$rdi")) & 0xFFFFFFFFFFFFFFFF
        guest = int(gdb.parse_and_eval("$rsi")) & 0xFFFFFFFF
        out.write("watching guest %08X at host %x\n" % (guest, host))
        out.flush()
        Watch(host, guest)
        return False


gdb.execute("set pagination off")
gdb.execute("set confirm off")
gdb.execute("handle SIGPIPE nostop noprint pass")
Entry("b3np_gdb_watch", internal=True)
gdb.execute("run")
code = gdb.parse_and_eval("$_exitcode")
out.write("exit %s\n" % code)
out.close()
gdb.execute("quit %d" % (int(code) if code.type.code != gdb.TYPE_CODE_VOID else 99))
