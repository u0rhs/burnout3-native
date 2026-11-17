// native_boot -- boots the retail game natively under the native backend (S0, docs/standalone.md §8) and reports how
// far it gets before the first function with no native version. No interpreter.
//
// The ELF is read from the user's install (data only: B3_ELF, default data/game or the perf install) and checked by size and
// FNV-1a 64. crt0 (_start, 0x100008) is host code here, step by step as retail [C decomp runtime/crt0.s]:
//   bss is zero (the arena starts cleared), sp = the main thread's stack top [G: SetupThread's return for stack
//   0x1FF0000 + 0x10000], then _InitSys, FlushCache(0), main(argc, argv) = func_0012EB30, exit(v0) -- each through the
//   one table, so the first missing one ends the run (strict; what a standalone build does). _InitSys, FlushCache and
//   exit are platform-tier stubs (src/native/platform.cpp); stubs that leave guest state different from retail are
//   listed at the end (the stub log).
//   --skip-crt0-lib  skip crt0's library calls instead of calling their stubs
//   --sinit          also run each of the static initialisers in the ctor table (0x4DD820-0x4DDA98) on its own, from
//                    the fresh image, and count how many complete natively; each one that completes is compared with
//                    the retail initialiser run from the same image in tests/r5900_mini.h (all RAM outside the stack,
//                    0x1FF0000-0x2000000): SAME / DIFF (or ORACLE when the retail run fails in the mini)
//   --entry ADDR     run one function from the fresh image instead of booting (args zero)
//   --disc-args      main's arguments as a disc boot gives them (argc 1, argv[0] = SYSTEM.CNF's BOOT2); default:
//                    argc 0, argv[0] NULL (the game's boot path D_0066E980 becomes "cdrom0:\(NULL);1"), as every
//                    proof and golden run has them
//   --static-init    after crt0, run main's first step alone: func_00100240 = all static initialisers in retail order
//                    (what main would do first), and report where that stops
//   --oracle ADDR[:K] (boot2) the boot oracle: at the K-th call (default 1) of the table function at ADDR during the
//                    boot, the machine state is kept; when the native call returns, the retail function runs in
//                    tests/r5900_mini.h from that state with the platform's hardware and kernel (boot_oracle.h), and the
//                    two are compared: the result register, all RAM outside the stack 0x1FF0000-0x2000000, the
//                    scratchpad, the kernel's handler lists and masks, and the hardware access traces (b3hw against
//                    the mini's MMIO log). Repeatable; the boot then goes on natively.
//   --poll-point [--pads PADS] [--polls N] (boot2) the poll point: each return of CGtInputManagerPS2Pad2::UpdatePort(0)
//                    once the front end's controller latch has started (poll_point below): the poll logs,
//                    B3NATIVE_MASKED_HASH and B3NATIVE_DMA_DUMP run there. With PADS (a pads.log: `P <poll> <vblank>
//                    <step> <cycles> <32-byte hex sample>` lines) the poll's sample is written at 0x4F52D8 first, else
//                    the neutral one. Stops after N polls (--polls N alone implies --poll-point).
//   --render offscreen|window [--render-lib LIB] [--render-args "..."] (pic1, docs/native-renderer.md §32) the game
//                    view: the scene / HUD capture at its hook points (gameview_native.h) into the in-process renderer
//                    (libb3render.so / b3render.dll: b3replay's live options; offscreen writes --shots to
//                    --offscreen-live <dir>, default build/gameview). Off by default; windowed runs are checked by hand.
//   --movie-log [N]  (movie2) logs the movies' picture counter and end (sceMpegIsEnd) with the virtual VBlank; stops
//                    after the N-th movie end (default 1)
//   --lookahead ADDR[:K] (boot2) at the K-th call of ADDR, before the native call: run the retail function from the
//                    boot's state in the mini (with the platform's hardware and kernel) to its return or to the first
//                    thing the mini cannot do, and list in order every function entry it reaches that has no native
//                    version (address, name, kind, calls) -- the boot's next stops without a rebuild per stop
#include "b3host/call.h"
#include "b3host/native_backend.h"
#include "b3asset/discfs.h"   // iso1: the ELF / SYSTEM.CNF from a disc folder or a disc image
#include "b3host/watch.h"
#include "native_table.h"
#include "platform.h"
#include "r5900_mini.h"
#include "boot_oracle.h"
// nofallback1: native_boot (the headless proof tool, never shipped) always has the fallback; native_play has it only
// in a NATIVE_FALLBACK=1 build (tools/native/native_fallback.h)
#undef B3_NATIVE_FALLBACK
#define B3_NATIVE_FALLBACK 1
#include "native_fallback.h"
#include "explore_standins.h"   // play1: exploration-only stand-ins (B3NATIVE_MOVIE_STANDIN)
#include "platform_sif.h"
#include "gameview_native.h"   // pic1

#include <algorithm>
#include <array>
#include <csignal>
#include <cctype>
#include <cstdio>
#include <sstream>
#ifdef _WIN32   // win1: the crash report through an unhandled-exception filter, the watchdog on a thread
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <chrono>
#include <thread>
#else
#include <dlfcn.h>
#include <execinfo.h>
#include <unistd.h>
#endif
#include <cstdlib>
#include <filesystem>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace {

#ifdef _WIN32
constexpr const char* kDefaultElf = "data/game/SLUS_210.50";   // the repo's git-ignored disc folder (--elf / B3_ELF)
#else
constexpr const char* kDefaultElf = "data/game/SLUS_210.50";   // the repo's git-ignored disc folder (--elf / B3_ELF)
#endif
constexpr std::uint64_t kElfSize = 4073816;                 // [C] retail SLUS_210.50
constexpr std::uint64_t kElfFnv = 0x790a7666929b62eaull;     // [C] FNV-1a 64 of the whole file
// boot2 (gen1's option A: the static initialisers retail): main's own frame [C func_0012EB30 addiu sp, -0xA0 before its
// first call, func_00100240]. Native main builds no guest frame, so its callees -- the ctor walker run retail in the
// fallback mini included -- would start 0xA0 higher than retail's; the guest sp is lowered by it while native main runs.
constexpr std::uint32_t kMainFrame = 0xA0u;
constexpr b3host::gaddr kStackTop = 0x01FFFD60u;             // [G] top of 0x1FF0000 + 0x10000 less the kernel's frame
constexpr b3host::gaddr kCtorBegin = 0x004DD820u, kCtorEnd = 0x004DDA98u;   // [C] crt0 func_00100240

std::string hex32(std::uint32_t v) {
    char b[16];
    std::snprintf(b, sizeof b, "%08X", v);
    return b;
}

bool load(b3host::NativeBackend& nb, const std::string& path) {
    std::vector<std::uint8_t> d;
    b3asset::disc::read_file(path, d);   // iso1: <image>/SLUS_210.50 too
    if (d.size() != kElfSize || b3host::fnv1a64(d.data(), d.size()) != kElfFnv) {
        std::printf("ELF %s: not the retail SLUS_210.50 (size %zu)\n", path.c_str(), d.size());
        return false;
    }
    std::string err;
    if (!nb.load_elf_bytes(d, path, err)) { std::printf("ELF: %s\n", err.c_str()); return false; }
    return true;
}

b3host::gaddr g_stop = 0;   // the address of the last MissingNative
struct PollLimit : std::runtime_error { using std::runtime_error::runtime_error; };   // --polls reached

// Why `addr` has no native function: its kind / C status (the decomp's list), and for game code with C the
// generator's verdict from a candidates.py TSV (B3NATIVE_CAND), so the census names the provider of each stop.
std::string why_missing(b3host::gaddr addr) {
    std::size_t n = 0;
    const b3host::FunctionInfo* info = b3native::functions(n);
    const b3host::FunctionInfo* f = nullptr;
    for (std::size_t k = 0; k < n; ++k) if (info[k].address == addr) { f = &info[k]; break; }
    if (!f) return "not a function entry in the decomp's list (a guest address that is no code: a generator gap)";
    const std::string kind = f->kind, c = f->c;
    if (kind != "game" && kind != "crt0") return kind + " library entry point: platform tier (S5) or the library as C";
    if (c == "none") return "game code without C: decomp S1 gap";
    std::string r = "game code with C (" + c + ") not in the native table";
    if (const char* cand = std::getenv("B3NATIVE_CAND")) {
        std::ifstream in(cand);
        const std::string key = hex32(addr) + "\t";
        for (std::string line; std::getline(in, line);)
            if (line.rfind(key, 0) == 0) {
                std::vector<std::string> col;
                std::size_t a = 0, b;
                while ((b = line.find('\t', a)) != std::string::npos) { col.push_back(line.substr(a, b - a)); a = b + 1; }
                col.push_back(line.substr(a));
                if (col.size() > 7) r += ": generator " + col[6] + (col[7].empty() ? "" : " (" + col[7] + ")");
                else if (col.size() > 6) r += ": generator " + col[6];
                if (col.size() > 6 && col[6] == "ok") r += " alone, but not in the combined generation (build/gen_native/failed.tsv)";
                break;
            }
    }
    return r;
}

// Distinct table entries entered through dispatch (a lower bound on the functions that ran natively: direct C++
// links between generated units bypass dispatch), by source; with B3NATIVE_ENTERED=<file> also each one.
void report_entered() {
    std::size_t n = 0;
    const b3native::TableEntry* e = b3native::install(n);
    std::map<std::string, unsigned> by;
    unsigned total = 0;
    std::uint64_t calls = 0;
    std::FILE* out = nullptr;
    if (const char* p = std::getenv("B3NATIVE_ENTERED")) out = std::fopen(p, "w");
    if (out) std::fprintf(out, "address\tname\tsource\tcalls\n");
    for (std::size_t k = 0; k < n; ++k) {
        const std::uint64_t c = b3native::entered(k);
        if (!c) continue;
        ++total;
        calls += c;
        std::string src = e[k].source;
        ++by[src];
        if (out) std::fprintf(out, "%s\t%s\t%s\t%llu\n", hex32(e[k].address).c_str(), e[k].name, e[k].source, (unsigned long long)c);
    }
    if (out) std::fclose(out);
    // gen1: B3NATIVE_TABLE=<file> lists every installed table entry (address order, as set_native_overrides sees them),
    // to compare two builds' tables
    if (const char* p = std::getenv("B3NATIVE_TABLE")) {
        if (std::FILE* t = std::fopen(p, "w")) {
            std::fprintf(t, "address\tname\tsource\tret\n");
            for (std::size_t k = 0; k < n; ++k)
                std::fprintf(t, "%s\t%s\t%s\t%c\n", hex32(e[k].address).c_str(), e[k].name, e[k].source, e[k].ret);
            std::fclose(t);
        }
    }
    std::string detail;
    for (const auto& [k, v] : by) detail += (detail.empty() ? "" : ", ") + k + " " + std::to_string(v);
    std::printf("ran natively: %u distinct table functions entered through dispatch (%llu calls; %s) [lower bound: "
                "direct C++ links not counted]\n", total, (unsigned long long)calls, detail.c_str());
}

struct Step {
    std::string what;
    std::string outcome;
    bool ok;
};

// Runs `fn` with `args` through the table; returns the outcome line.
Step call(b3host::NativeBackend& nb, const std::string& what, b3host::gaddr fn, b3host::CallArgs args) {
    args.sp = nb.stack_pointer();
    b3host::CallResult r;
    try {
        nb.run(fn, args, r);
        return {what, "ok (v0 " + hex32(r.v0.w[0]) + ")", true};
    } catch (const PollLimit& e) {
        return {what, std::string("ran until the limit: ") + e.what(), false};
    } catch (const b3native::platform::VBlankLimit& e) {
        return {what, std::string("ran until the limit: ") + e.what(), false};
    } catch (const b3native::platform::NativeExit& e) {
        return {what, std::string("program ended: ") + e.what(), false};
    } catch (const b3host::MissingNative& e) {
        g_stop = e.address;
        return {what, std::string("MISSING ") + e.what(), false};
    } catch (const b3host::NativeFault& e) {
        return {what, std::string("FAULT ") + e.what(), false};
    } catch (const std::exception& e) {
        // boot2: the chain array is not cleared when the scopes unwind: its first entries are the chain at the throw
        // (deeper stale entries may follow from earlier, deeper calls)
        std::string c;
        for (unsigned k = 0; k < 24 && b3host::native_chain::t_chain[k]; ++k)
            c += (k ? " > " : "") + hex32(b3host::native_chain::t_chain[k]) + " " + b3host::describe_function(b3host::native_chain::t_chain[k]);
        return {what, std::string("ERROR ") + e.what() + "; chain array: " + c, false};
    }
}

b3host::CallArgs args_of(std::initializer_list<std::uint32_t> a) {
    b3host::CallArgs c;
    unsigned i = 0;
    for (std::uint32_t v : a) c.gpr[i++] = b3host::detail::gpr64(b3host::detail::sext32(v));
    c.n_gpr = 8;
    c.n_fpr = 8;
    return c;
}

// crt0's SetupThread (syscall 0x3C, a3 = 0x1D9CE80 [C crt0.s 0x1001A4]): the kernel copies the program's arguments
// into that buffer, `struct { int argc; char *argv[16]; char payload[256]; }` [L ps2sdk ee/startup/include/startup.h
// struct sargs], and crt0 passes argc = word 0, argv = buffer + 4 to main [C crt0.s 0x1001FC-0x10020C]. A disc boot
// starts the program from SYSTEM.CNF's BOOT2 line with that path as argv[0] [C install SYSTEM.CNF: `BOOT2 =
// cdrom0:\SLUS_210.50;1`; L: the OSD passes it as the only argument]. main builds the boot path from argv[0]
// (func_0012EB30: snprintf "%s", strstr "cdrom0:"). Read from the SYSTEM.CNF beside the ELF; the stub log says so.
void setup_boot_args(const std::string& elf) {
    std::string boot = "cdrom0:\\SLUS_210.50;1";
    const std::string cnf = elf.substr(0, elf.find_last_of("/\\") + 1) + "SYSTEM.CNF";
    std::vector<std::uint8_t> cnf_bytes;   // iso1: beside the ELF in a disc folder or in a disc image
    b3asset::disc::read_file(cnf, cnf_bytes);
    std::istringstream in(std::string(cnf_bytes.begin(), cnf_bytes.end()));
    for (std::string line; std::getline(in, line);) {
        if (line.rfind("BOOT2", 0) != 0) continue;
        std::size_t a = line.find('=');
        if (a == std::string::npos) continue;
        std::string v = line.substr(a + 1);
        while (!v.empty() && (v.front() == ' ' || v.front() == '\t')) v.erase(0, 1);
        while (!v.empty() && (v.back() == ' ' || v.back() == '\r' || v.back() == '\n')) v.pop_back();
        if (!v.empty() && v.size() < 256) boot = v;
    }
    constexpr b3host::gaddr kArgs = 0x01D9CE80u, kPayload = kArgs + 4 + 16 * 4;
    b3host::store<std::uint32_t>(kArgs, 1u);
    b3host::store<std::uint32_t>(kArgs + 4, kPayload);
    for (std::size_t k = 0; k <= boot.size(); ++k)
        b3host::store<std::uint8_t>(kPayload + static_cast<b3host::gaddr>(k), k < boot.size() ? static_cast<std::uint8_t>(boot[k]) : 0);
    b3native::platform::stub_note("SetupThread", "boot arguments modelled: argc 1, argv[0] = \"" + boot +
                                                     "\" (SYSTEM.CNF BOOT2) at 0x1D9CE80 [L: the OSD's disc boot]");
}

// ---- the boot oracle (boot2) ----
struct Oracle {
    b3host::gaddr target = 0;
    unsigned want = 1, seen = 0, nest = 0;
    bool armed = false, done = false;
    std::vector<std::uint8_t> ram, spr;
    b3host::CallArgs args;
    bool eie = true;
    b3asm::State vu;
    std::uint32_t count = 0;   // play1: COP0 Count when the state was kept
    b3native::KernelModel kernel;
    b3native::HwModelDev hw;
    std::vector<b3host::HwAccess> trace;
    std::string verdict;
};
std::vector<Oracle> g_oracles;
std::string g_elf;

// The handler argument is left out: AddIntcHandler / AddDmacHandler take three arguments and the kernel keeps
// whatever a3 holds [C retail SkyStart -> _rwDMAHook: a3 0xFFFFF000, a leftover; natively 0]; the handlers that are
// registered so (RW's) never read it [C rwDMAHandler / rwDMAVSYNCHandler read only a0].
std::string kernel_text(const b3native::KernelModel& k) {
    std::string t;
    for (const auto& h : k.intc) t += "i" + std::to_string(h.id) + ":" + std::to_string(h.cause) + ":" + hex32(h.handler) + " ";
    for (const auto& h : k.dmac) t += "d" + std::to_string(h.id) + ":" + std::to_string(h.cause) + ":" + hex32(h.handler) + " ";
    for (const auto& x : k.semas) t += "s" + std::to_string(x.id) + ":" + std::to_string(x.count) + "/" + std::to_string(x.max) + " ";
    for (const auto& x : k.threads) t += "t" + std::to_string(x.id) + ":" + hex32(x.entry) + ":" + hex32(x.stack) + ":" + std::to_string(x.priority) + (x.started ? "+" : "") + " ";
    return t + "mask " + hex32(k.intc_mask) + "/" + hex32(k.dmac_mask) + " wake " + std::to_string(k.wakeups) + " vbl " + std::to_string(k.vblanks);
}

void oracle_finish(Oracle& o, b3host::gaddr f, const b3host::CallResult& res) {
    b3host::set_hw_trace(nullptr);
    auto& nb = static_cast<b3host::NativeBackend&>(b3host::backend());
    auto cpu = std::make_unique<mini::R5900>();
    std::string v;
    if (!cpu->load_elf(g_elf.c_str())) { o.verdict = "ORACLE no ELF"; return; }
    cpu->ram() = o.ram;
    std::memcpy(cpu->spr().data(), o.spr.data(), std::min(o.spr.size(), cpu->spr().size()));
    cpu->max_steps = 200000000;
    for (unsigned r = 0; r < 8; ++r) cpu->set_gpr128(4 + r, b3asm::make_u128(o.args.gpr[r].w[0], o.args.gpr[r].w[1], o.args.gpr[r].w[2], o.args.gpr[r].w[3]));
    for (unsigned r = 0; r < 8; ++r) cpu->set_fpr(12 + r, o.args.fpr[r]);
    cpu->set_gpr64(28, 0x004E8670u);
    cpu->set_gpr64(29, o.args.sp);
    cpu->cop0_status = o.eie ? 0x10001u : 0x1u;
    cpu->vu = o.vu;
    // play1: mfc0 Count reads the backend's virtual clock as it was at the call's entry (what the native call read),
    // not the replay's later time (CGtHiResSystemTimer::GetTime 0x375280); a function that reads it twice sees one value
    cpu->count_source = [c = o.count] { return c; };
    b3native::MiniPlatform mp(*cpu);
    mp.hw = o.hw;
    mp.k = o.kernel;
    mp.install();
    try {
        cpu->run_from(f);
    } catch (const std::exception& e) {
        o.verdict = std::string("ORACLE retail run failed: ") + e.what();
        return;
    }
    std::string d;
    int nd = 0;
    auto note = [&](const std::string& s) { if (++nd <= 8) d += (d.empty() ? "" : "; ") + s; };
    const b3native::TableEntry* te = b3native::entry_at(f);
    const char ret = te ? te->ret : 'i';
    const auto v0 = cpu->gpr128(2);
    if ((ret == 'i' && v0.w[0] != res.v0.w[0]) || (ret == 'l' && b3asm::lo64(v0) != b3host::detail::low64(res.v0)))
        note("v0 retail " + hex32(v0.w[0]) + " native " + hex32(res.v0.w[0]));
    if (ret == 'f' && cpu->fpr(0) != res.f0) note("f0 retail " + hex32(cpu->fpr(0)) + " native " + hex32(res.f0));
    unsigned words = 0;
    for (std::uint32_t x = 0; x + 4 <= 0x01FF0000u; x += 4)
        if (std::memcmp(cpu->ram().data() + x, nb.ram().data() + x, 4) != 0) {
            ++words;
            std::uint32_t a, b;
            std::memcpy(&a, cpu->ram().data() + x, 4);
            std::memcpy(&b, nb.ram().data() + x, 4);
            note("mem " + hex32(x) + " retail " + hex32(a) + " native " + hex32(b));
        }
    for (std::uint32_t x = 0; x < 0x4000; x += 4)
        if (std::memcmp(cpu->spr().data() + x, nb.scratchpad().data() + x, 4) != 0) { ++words; note("spr " + hex32(0x70000000u + x)); }
    if (kernel_text(mp.k) != kernel_text(b3native::platform::kernel()))
        note("kernel retail [" + kernel_text(mp.k) + "] native [" + kernel_text(b3native::platform::kernel()) + "]");
    const std::string t = b3native::compare_hw_traces(mp.trace, o.trace);
    if (!t.empty()) note(t);
    unsigned written = 0;
    for (std::uint32_t x = 0; x + 4 <= 0x01FF0000u; x += 4)
        if (std::memcmp(cpu->ram().data() + x, o.ram.data() + x, 4) != 0) ++written;
    o.verdict = nd ? "DIFF (" + std::to_string(words) + " words) " + d
                   : "SAME as retail (" + std::to_string(written) + " RAM words written, " + std::to_string(o.trace.size()) +
                         " hardware accesses, equal in order and value)";
}

// ---- lookahead (boot2) ----
struct Lookahead {
    b3host::gaddr target = 0;
    unsigned want = 1, seen = 0;
    bool done = false;
};
Lookahead g_look;
// B3NATIVE_LOOK_STEPS: the retail run's instruction limit (default 2e9)
const std::uint64_t g_look_steps = [] { const char* e = std::getenv("B3NATIVE_LOOK_STEPS"); return e && *e ? std::strtoull(e, nullptr, 10) : 2000000000ull; }();

void lookahead_run(b3host::gaddr f, const b3host::CallArgs& a) {
    auto& nb = static_cast<b3host::NativeBackend&>(b3host::backend());
    auto cpu = std::make_unique<mini::R5900>();
    if (!cpu->load_elf(g_elf.c_str())) return;
    std::memcpy(cpu->ram().data(), nb.ram().data(), mini::R5900::kRamSize);
    std::memcpy(cpu->spr().data(), nb.scratchpad().data(), std::min(nb.scratchpad().size(), cpu->spr().size()));
    for (unsigned r = 0; r < 8; ++r) cpu->set_gpr128(4 + r, b3asm::make_u128(a.gpr[r].w[0], a.gpr[r].w[1], a.gpr[r].w[2], a.gpr[r].w[3]));
    for (unsigned r = 0; r < 8; ++r) cpu->set_fpr(12 + r, a.fpr[r]);
    cpu->set_gpr64(28, 0x004E8670u);
    cpu->set_gpr64(29, a.sp);
    cpu->cop0_status = nb.interrupts_enabled() ? 0x10001u : 0x1u;
    cpu->vu = b3asm::state();
    b3native::MiniPlatform mp(*cpu);
    mp.hw = b3native::platform::hw_model();
    mp.k = b3native::platform::kernel();
    mp.install();
    // Sony library entries the platform tier stubs (IOP / CD / SIF / scf: no guest memory of their own written) are
    // answered by the platform stub, as natively, instead of running retail's library internals in the mini
    std::size_t nt = 0;
    const b3native::TableEntry* te = b3native::install(nt);
    for (std::size_t k = 0; k < nt; ++k) {
        if (std::string(te[k].source) != "platform" || std::strncmp(te[k].name, "sce", 3) != 0) continue;
        const b3host::NativeOverride* o = b3host::native_override(te[k].address);
        if (!o) continue;
        auto entry = o->entry;
        cpu->hooks[te[k].address] = [entry](mini::R5900& c) {
            b3host::CallArgs ca;
            for (unsigned r = 0; r < 8; ++r) ca.gpr[r] = c.gpr128(4 + r);
            ca.n_gpr = ca.n_fpr = 8;
            ca.sp = static_cast<b3host::gaddr>(c.gpr_lo(29));
            b3host::CallResult cr;
            entry(ca, cr);
            c.set_gpr128(2, cr.v0);
        };
    }
    std::size_t n = 0;
    const b3host::FunctionInfo* info = b3native::functions(n);
    std::map<b3host::gaddr, const b3host::FunctionInfo*> starts;
    for (std::size_t k = 0; k < n; ++k) starts[info[k].address] = &info[k];
    std::vector<b3host::gaddr> order;
    std::map<b3host::gaddr, unsigned> calls;
    std::map<b3host::gaddr, std::uint64_t> all_calls;   // every function entry (native or not) outside quiet regions
    std::string end = "returned";
    std::uint64_t steps = 0;
    cpu->max_steps = ~0ull;   // the lookahead's own limit (B3NATIVE_LOOK_STEPS) applies
    cpu->set_gpr64(31, mini::R5900::kSentinel);
    cpu->set_pc(f);
    try {
        // inside a platform-tier or libm function (hand-written native code that calls no retail code) nothing is
        // recorded until it returns (pc back at its ra with the stack unwound); generated code calls its callees
        // through the table, so their entries count
        std::uint32_t quiet_ra = 0, quiet_sp = 0;
        while (cpu->pc() != mini::R5900::kSentinel) {
            if (quiet_ra && cpu->pc() == quiet_ra && static_cast<std::uint32_t>(cpu->gpr_lo(29)) >= quiet_sp) quiet_ra = 0;
            auto it = starts.find(cpu->pc());
            if (it != starts.end() && !quiet_ra && cpu->pc() != f) {
                ++all_calls[cpu->pc()];
                const b3native::TableEntry* e = b3native::entry_at(cpu->pc());
                if (!e) {
                    if (!calls[cpu->pc()]++) order.push_back(cpu->pc());
                } else if (std::string(e->source) == "platform" || std::string(e->source) == "libm") {
                    quiet_ra = static_cast<std::uint32_t>(cpu->gpr_lo(31));
                    quiet_sp = static_cast<std::uint32_t>(cpu->gpr_lo(29));
                }
            }
            if (auto h = cpu->hooks.find(cpu->pc()); h != cpu->hooks.end()) {
                h->second(*cpu);
                cpu->set_pc(static_cast<std::uint32_t>(cpu->gpr_lo(31)));
                continue;
            }
            cpu->step_one();
            mp.tick();
            if (++steps > g_look_steps) { end = "step limit"; break; }
        }
    } catch (const std::exception& e) {
        end = std::string("stopped: ") + e.what() + " (in " + b3host::describe_function(cpu->pc()) + ")";
    }
    std::printf("lookahead %s %s: retail %s after %llu instructions; %zu functions without a native version, in order:\n",
                hex32(f).c_str(), b3host::describe_function(f).c_str(), end.c_str(), (unsigned long long)steps, order.size());
    for (b3host::gaddr g : order) {
        const b3host::FunctionInfo* fi = starts[g];
        std::printf("  %s\t%s\t%s\t%s\tsize %u\tcalls %u\n", hex32(g).c_str(), fi->name, fi->kind, fi->c, fi->size, calls[g]);
    }
    std::vector<std::pair<std::uint64_t, b3host::gaddr>> top;
    for (const auto& [g, c] : all_calls) top.push_back({c, g});
    std::sort(top.rbegin(), top.rend());
    std::printf("lookahead: retail's most entered functions:\n");
    for (std::size_t k = 0; k < top.size() && k < 12; ++k)
        std::printf("  %s\t%s\t%llu\n", hex32(top[k].second).c_str(), b3host::describe_function(top[k].second).c_str(),
                    (unsigned long long)top[k].first);
    std::printf("lookahead: retail VBlanks %llu, DMAs %llu\n", (unsigned long long)mp.k.vblanks, (unsigned long long)mp.hw.dma_count());
    std::fflush(stdout);
}

// ---- the poll point (boot2) ----
struct PollPoint {
    bool on = false;
    std::map<std::uint64_t, std::array<unsigned char, 32>> samples;   // pads.log P lines
    std::uint64_t poll = 0, max_polls = 0;
};
PollPoint g_poll;
constexpr b3host::gaddr kUpdatePort = 0x00214610u;   // [C] UpdatePort__22CGtInputManagerPS2Pad2F15EGtPS2InputPort

bool poll_setup(const std::string& pads, std::uint64_t n) {
    if (!pads.empty()) {
        std::ifstream pf(pads);
        if (!pf) { std::printf("--pads: cannot open %s\n", pads.c_str()); return false; }
        for (std::string line; std::getline(pf, line);) {
            unsigned long long p = 0, vb = 0, st = 0, cy = 0;
            char hex[80] = {};
            if (std::sscanf(line.c_str(), "P %llu %llu %llu %llu %64s", &p, &vb, &st, &cy, hex) != 5) continue;
            std::array<unsigned char, 32> smp{};
            for (unsigned k = 0; k < 32; ++k) smp[k] = static_cast<unsigned char>(std::strtoul(std::string(hex + 2 * k, 2).c_str(), nullptr, 16));
            g_poll.samples[p] = smp;
        }
    }
    g_poll.max_polls = n;
    g_poll.on = true;
    return true;
}

// The controller latch at the poll point (the recorded bundles' pad model): it starts at the first
// poll where the front end's phase word 0x01E90424 is 5 and its flow 0x01EA2974 is 0x01EA2DB8 (the manager at 0x4F50C0,
// its vtable 0x4DDFF0 at 0x4F5340), and from then on runs at every poll: the DS2 profile's 40 channel descriptors at
// 0x676494 seeded, the 32-byte sample at 0x4F52D8 (here the bundle's recorded one), then 0x4F52D0 = 4, 0x4F5318 = 0,
// byte 0x4F6065 = 1. Polls are counted from that start (a pads.log's poll 1).
bool g_latch_started = false;
void seed_ds2_profile(std::uint8_t* ram) {
    auto w32 = [&](std::uint32_t a, std::uint32_t v) { std::memcpy(ram + a, &v, 4); };
    for (std::uint32_t ch = 0; ch != 40; ++ch) {
        const std::uint32_t d = 0x676494u + ch * 16u;
        w32(d, 0); w32(d + 4, 0); w32(d + 8, 0); w32(d + 12, 0);
        if (ch < 16) { w32(d, 1); w32(d + 4, 1); w32(d + 8, ch >> 3); w32(d + 12, ch & 7); }
        else if (ch < 32) { w32(d, 1); w32(d + 4, 8); w32(d + 8, ch - 14); }
        else if (ch >= 35 && ch < 39) { w32(d, 1); w32(d + 4, 8); w32(d + 8, ch - 17); }
    }
}
// vu0: B3NATIVE_MASKED_HASH=<file> (hex RAM word addresses, one per line, '#' comments; config/compare_mask.tsv works,
// its first column): at every poll point, "maskhash <poll> <fnv1a-64>" of the whole 32 MB RAM with those words read as
// zero -- two runs (e.g. a B3NATIVE_POISON run and its control) agree on RAM outside the mask at every poll iff the lists
// agree (the poison-proof condition, 2026-11-07)
void masked_hash_point(std::uint64_t poll) {
    static const char* path = std::getenv("B3NATIVE_MASKED_HASH");
    if (!path || !*path) return;
    static std::vector<std::uint32_t> words = [] {
        std::vector<std::uint32_t> v;
        if (std::FILE* f = std::fopen(path, "r")) {
            char line[8192];
            while (std::fgets(line, sizeof line, f))
                if (line[0] != '#' && std::isxdigit(static_cast<unsigned char>(line[0])))
                    v.push_back(static_cast<std::uint32_t>(std::strtoul(line, nullptr, 16)) & 0x01FFFFFCu);
            std::fclose(f);
        }
        std::sort(v.begin(), v.end());
        std::printf("native_boot: B3NATIVE_MASKED_HASH: %zu masked words\n", v.size());
        return v;
    }();
    auto& nb = static_cast<b3host::NativeBackend&>(b3host::backend());
    const std::uint8_t* ram = nb.ram().data();
    std::uint64_t h = 0xcbf29ce484222325ull;
    std::size_t k = 0;
    for (std::uint32_t a = 0; a < 0x2000000u; a += 4) {
        std::uint32_t w;
        std::memcpy(&w, ram + a, 4);
        while (k < words.size() && words[k] < a) ++k;
        if (k < words.size() && words[k] == a) w = 0;
        h = (h ^ w) * 0x100000001b3ull;
    }
    static std::uint64_t stream = 0xcbf29ce484222325ull;   // the VIF1 / GIF words read since the last poll point
    auto& hwm = b3native::platform::hw_model();
    // vu0: B3NATIVE_DMA_DUMP=<file>[,<first poll>,<last poll>]: the words hashed into "dma" as text ("<poll> <channel>
    // <guest addr> <word>", the poll the words lead up to), optionally only between two polls
    static std::FILE* dma_dump = nullptr;
    static unsigned long long dma_lo = 0, dma_hi = 0, dma_poll = 0;
    dma_poll = poll + 1;
    if (dma_dump) std::fflush(dma_dump);
    static bool hooked = false;
    if (!hooked) {
        hooked = true;
        if (const char* d = std::getenv("B3NATIVE_DMA_DUMP")) {
            std::string path(d);
            const std::size_t c = path.find(',');
            if (c != std::string::npos) {
                std::sscanf(path.c_str() + c + 1, "%llu,%llu", &dma_lo, &dma_hi);
                path.resize(c);
            }
            dma_dump = std::fopen(path.c_str(), "w");
            if (!dma_dump) b3host::fatal("native_boot: B3NATIVE_DMA_DUMP: cannot open " + path);
        }
        // H5: the walk is the dev model's (hw_model_dev.h hash_dma_words), on the kick's registers (HwModel::on_dma)
        auto prev = hwm.on_dma;
        hwm.on_dma = [prev](unsigned c, std::uint32_t chcr, std::uint32_t madr, std::uint32_t qwc, std::uint32_t tadr) {
            if (prev) prev(c, chcr, madr, qwc, tadr);
            const std::uint32_t regs[4] = {chcr, madr, qwc, tadr};
            b3native::hash_dma_words(c, regs, stream,
                [](std::uint32_t a) { std::uint32_t v = 0; std::memcpy(&v, b3host::host_pointer(a, 4), 4); return v; },
                [](std::uint32_t a, std::uint32_t w, unsigned ch) {
                    if (!dma_dump || ((dma_lo || dma_hi) && (dma_poll < dma_lo || dma_poll > dma_hi))) return;
                    std::fprintf(dma_dump, "%llu %u %08X %08X\n", dma_poll, ch, a, w);
                });
        };
    }
    std::printf("maskhash %llu %016llx dma %016llx\n", (unsigned long long)poll, (unsigned long long)h, (unsigned long long)stream);
    stream = 0xcbf29ce484222325ull;
}

void poll_point() {
    if (std::getenv("B3NATIVE_RWATCH")) std::printf("rwatch: poll %llu\n", (unsigned long long)(g_poll.poll + 1));   // vu0
    auto& nb = static_cast<b3host::NativeBackend&>(b3host::backend());
    std::uint8_t* ram = nb.ram().data();
    auto r32 = [&](std::uint32_t a) { std::uint32_t v; std::memcpy(&v, ram + a, 4); return v; };
    if (!g_latch_started) {
        // movie2: B3NATIVE_LATCH_LOG=1 prints the latch's three inputs at each UpdatePort(0) return until it starts
        static const bool latch_log = std::getenv("B3NATIVE_LATCH_LOG") != nullptr;
        static std::uint64_t pre = 0;
        ++pre;
        if (latch_log)
            std::printf("latch: call %llu VBlank %llu vtable %08X phase %u flow %08X\n", static_cast<unsigned long long>(pre),
                        static_cast<unsigned long long>(b3native::platform::kernel().vblanks), r32(0x4F5340u),
                        r32(0x01E90424u), r32(0x01EA2974u));
        if (!(r32(0x4F5340u) == 0x004DDFF0u && r32(0x01E90424u) == 5u && r32(0x01EA2974u) == 0x01EA2DB8u)) return;
        g_latch_started = true;
    }
    const std::uint64_t poll = ++g_poll.poll;
    b3native::platform::log_poll = poll;
    if (poll == 1 && std::getenv("B3NATIVE_SEMA_LIST"))   // boot2: the kernel's semaphores at poll 1
        for (const auto& m : b3native::platform::kernel().semas)
            std::printf("sema %d count %d max %d attr %x option %x\n", m.id, m.count, m.max, m.attr, m.option);
    // movie2: the virtual VBlank of the first polls (a pads.log line: P <poll> <vblank> ...)
    if (poll <= 3 || std::getenv("B3NATIVE_POLL_LOG"))   // sync1: every poll with B3NATIVE_POLL_LOG (pads.log's P <poll> <vblank> <step> <cycles>)
        std::printf("poll: %llu at VBlank %llu cycles %llu\n", static_cast<unsigned long long>(poll),
                    static_cast<unsigned long long>(b3native::platform::kernel().vblanks),
                    static_cast<unsigned long long>(b3native::platform::virtual_cycles()));
    masked_hash_point(poll);   // vu0 (before this poll's pad sample goes in: the same point in both runs)
    seed_ds2_profile(ram);
    auto it = g_poll.samples.find(poll);
    if (it != g_poll.samples.end()) std::memcpy(ram + 0x4F52D8u, it->second.data(), 32);
    else std::memcpy(ram + 0x4F52D8u, b3native::platform::sif::neutral_pad_sample(), 32);
    const std::uint32_t four = 4, zero = 0;
    std::memcpy(ram + 0x4F52D0u, &four, 4);
    std::memcpy(ram + 0x4F5318u, &zero, 4);
    ram[0x4F6065u] = 1;
    if (g_poll.max_polls && poll >= g_poll.max_polls) throw PollLimit("poll limit " + std::to_string(poll));
}

// movie2: --movie-log [N] -- the movies' picture count and end tick: each return of sceMpegIsEnd (0x10A028 [C decomp
// libmpeg.s]; CGtVideoDecoder::Update asks it after each picture) logs libmpeg's picture counter (sceMpeg +8, the
// index of the last picture returned [C build/mpeg_play: 237 after CRI_RW30's 238 pictures]) when it changes, and the
// end (IsEnd 1), with the virtual VBlank count; the run stops after the N-th movie's end (default 1).
struct MovieLog {
    bool on = false;
    unsigned ends_wanted = 1, ends = 0;
    std::uint32_t last_mp = 0, last_count = 0xFFFFFFFFu;
    bool ended = false;
    std::uint64_t pictures = 0, first_vblank = 0;
};
MovieLog g_movie;
constexpr b3host::gaddr kMpegIsEnd = 0x0010A028u;
void movie_hook(const b3host::CallArgs& a, const b3host::CallResult& r) {
    auto& nb = static_cast<b3host::NativeBackend&>(b3host::backend());
    const std::uint32_t mp = a.gpr[0].w[0];
    if ((mp & 0x1FFFFFFFu) + 12 > nb.ram().size()) return;
    std::uint32_t count;
    std::memcpy(&count, nb.ram().data() + (mp & 0x1FFFFFFFu) + 8, 4);
    const std::uint64_t vb = b3native::platform::kernel().vblanks;
    const bool end = r.v0.w[0] != 0;
    if (mp != g_movie.last_mp) {   // a new decoder object: a new movie
        g_movie.last_mp = mp;
        g_movie.last_count = 0xFFFFFFFFu;
        g_movie.ended = false;
        g_movie.pictures = 0;
        g_movie.first_vblank = vb;
        std::printf("movie: decoder %08X first IsEnd at VBlank %llu\n", mp, static_cast<unsigned long long>(vb));
    }
    if (count != g_movie.last_count) {
        if (g_movie.last_count == 0xFFFFFFFFu || count != g_movie.last_count) ++g_movie.pictures;
        if (count < 4 || count % 60 == 0) std::printf("movie: picture counter %u at VBlank %llu\n", count, static_cast<unsigned long long>(vb));
        g_movie.last_count = count;
    }
    if (end && !g_movie.ended) {
        g_movie.ended = true;
        std::printf("movie: END (sceMpegIsEnd 1) decoder %08X picture counter %u, %llu counter changes, VBlank %llu (%llu after the first IsEnd)\n",
                    mp, count, static_cast<unsigned long long>(g_movie.pictures), static_cast<unsigned long long>(vb),
                    static_cast<unsigned long long>(vb - g_movie.first_vblank));
        std::fflush(stdout);
        if (++g_movie.ends >= g_movie.ends_wanted) throw PollLimit("movie end " + std::to_string(g_movie.ends));
    }
}

void oracle_hook(b3host::gaddr f, const b3host::CallArgs& a, const b3host::CallResult* r) {
    if (g_movie.on && f == kMpegIsEnd && r) movie_hook(a, *r);
    // UpdatePort is a member: $a0 = this, $a1 = the port (play1 found the $a0 test, which never matched natively)
    if (g_poll.on && f == kUpdatePort && r && a.gpr[1].w[0] == 0) poll_point();
    if (g_look.target == f && !r && !g_look.done && ++g_look.seen == g_look.want) {
        g_look.done = true;
        lookahead_run(f, a);
    }
    for (Oracle& o : g_oracles) {
        if (o.target != f || o.done) continue;
        if (!r) {
            if (o.armed) { ++o.nest; continue; }
            if (++o.seen != o.want) continue;
            auto& nb = static_cast<b3host::NativeBackend&>(b3host::backend());
            o.ram.assign(nb.ram().begin(), nb.ram().begin() + mini::R5900::kRamSize);
            o.spr.assign(nb.scratchpad().begin(), nb.scratchpad().end());
            o.args = a;
            o.eie = nb.interrupts_enabled();
            o.count = nb.cop0_count();   // play1: COP0 Count at the call's entry, for the retail replay
            o.vu = b3asm::state();
            o.kernel = b3native::platform::kernel();
            o.hw = b3native::platform::hw_model();
            // boot2: kept for a replay later: the queued completions' ready times made relative to now, the native
            // clock's closure dropped (MiniPlatform::install re-bases from 0 onto the mini's retired count)
            o.hw.rebase_completions(o.hw.clock ? o.hw.clock() : 0, 0);
            o.hw.clock = nullptr;
            o.trace.clear();
            b3host::set_hw_trace(&o.trace);
            o.armed = true;
        } else if (o.armed) {
            if (o.nest) { --o.nest; continue; }
            o.armed = false;
            o.done = true;
            oracle_finish(o, f, *r);
            std::printf("oracle %s %s (call %u): %s\n", hex32(f).c_str(), b3host::describe_function(f).c_str(), o.want,
                        o.verdict.c_str());
            std::fflush(stdout);
        }
    }
}

}  // namespace

// vu0 diagnostics: B3NATIVE_BYTEWATCH=<addr> (hex): at every dispatched native call's entry and exit (b3host/watch.h,
// all bits), the byte at addr; each change is printed with the event and the native chain (first 20). The first exit
// event after a change names the innermost dispatched function that wrote it (or the write lies in its caller between
// two events); direct C++ links are not separate events.
namespace vu0bw {
std::uint32_t g_addr = 0;
int g_last = -1;
unsigned g_reports = 0;
std::vector<std::uint64_t> g_bits;
void hook(b3host::gaddr f, const b3host::CallArgs&, const b3host::CallResult* r) {
    auto& nb = static_cast<b3host::NativeBackend&>(b3host::backend());
    const int v = nb.ram()[g_addr];
    if (g_last >= 0 && v != g_last && g_reports < 20) {
        ++g_reports;
        std::printf("bytewatch: %08X %02X -> %02X at %s of %08X %s; chain", g_addr, g_last, v, r ? "exit" : "entry", f,
                    b3host::describe_function(f).c_str());
        for (unsigned k = 0; k < b3host::native_chain::t_depth && k < b3host::native_chain::kMax; ++k)
            std::printf(" %08X", b3host::native_chain::t_chain[k]);
        std::printf("\n");
        std::fflush(stdout);
    }
    g_last = v;
}
// B3NATIVE_RWATCH=<lo>-<hi>: also every dispatched native call with an argument register within 0x400 bytes of the
// range (a native that may read it unseen by the mini's load watch), first 2000 entries
std::uint32_t g_rlo = 0, g_rhi = 0;
void rhook(b3host::gaddr f, const b3host::CallArgs& a, const b3host::CallResult* r) {
    static unsigned n = 0;
    if (r) return;
    for (unsigned k = 0; k < 8; ++k) {
        const std::uint32_t v = a.gpr[k].w[0] & 0x01FFFFFFu;
        if (v + 0x400 > g_rlo && v < g_rhi + 0x400) {
            if (++n <= 2000)
                std::printf("rwatch: native %08X %s arg%u %08X\n", f, b3host::describe_function(f).c_str(), k, a.gpr[k].w[0]);
            return;
        }
    }
}
// B3NATIVE_CALLLOG=<addr>: every dispatched call of that native: a0-a3, the 0x40 bytes at a0 at entry and exit, v0 (first 400)
std::uint32_t g_clog = 0;
void chook(b3host::gaddr f, const b3host::CallArgs& a, const b3host::CallResult* r) {
    static unsigned n = 0;
    if (f != g_clog || n >= 800) return;
    ++n;
    auto& nb = static_cast<b3host::NativeBackend&>(b3host::backend());
    const std::uint32_t p = a.gpr[0].w[0] & 0x01FFFFFFu;
    std::printf("calllog %s a0 %08X a1 %08X a2 %08X a3 %08X", r ? "exit " : "entry", a.gpr[0].w[0], a.gpr[1].w[0], a.gpr[2].w[0], a.gpr[3].w[0]);
    if (r) std::printf(" v0 %08X", r->v0.w[0]);
    if (p + 0x40 <= 0x2000000u) {
        std::printf(" mem");
        for (unsigned k = 0; k < 0x40; k += 4) { std::uint32_t w; std::memcpy(&w, nb.ram().data() + p + k, 4); std::printf(" %08X", w); }
    }
    std::printf("\n");
}
void install() {
    if (const char* c = std::getenv("B3NATIVE_CALLLOG")) {
        g_clog = static_cast<std::uint32_t>(std::strtoul(c, nullptr, 16));
        g_bits.assign(((b3host::kWatchHi - b3host::kWatchLo) >> 2) / 64 + 1, ~std::uint64_t{0});
        b3host::g_watch_bits = g_bits.data();
        b3host::g_watch_hook = chook;
        return;
    }
    if (const char* w = std::getenv("B3NATIVE_RWATCH")) {
        g_rlo = static_cast<std::uint32_t>(std::strtoul(w, nullptr, 16));
        const char* dash = std::strchr(w, '-');
        g_rhi = dash ? static_cast<std::uint32_t>(std::strtoul(dash + 1, nullptr, 16)) : g_rlo + 4;
        g_bits.assign(((b3host::kWatchHi - b3host::kWatchLo) >> 2) / 64 + 1, ~std::uint64_t{0});
        b3host::g_watch_bits = g_bits.data();
        b3host::g_watch_hook = rhook;
        std::printf("native_boot: B3NATIVE_RWATCH %08X-%08X\n", g_rlo, g_rhi);
        return;
    }
    const char* a = std::getenv("B3NATIVE_BYTEWATCH");
    if (!a || !*a) return;
    g_addr = static_cast<std::uint32_t>(std::strtoul(a, nullptr, 16));
    g_bits.assign(((b3host::kWatchHi - b3host::kWatchLo) >> 2) / 64 + 1, ~std::uint64_t{0});
    b3host::g_watch_bits = g_bits.data();
    b3host::g_watch_hook = hook;
    std::printf("native_boot: B3NATIVE_BYTEWATCH %08X\n", g_addr);
}
}  // namespace vu0bw

// boot2: B3NATIVE_WATCHDOG=<seconds>: then the native call chain (dispatch-entered functions) is printed and the run
// ends -- where a boot that neither stops nor reaches VBlanks is spinning
void watchdog(int) {
    std::fprintf(stdout, "watchdog: native chain depth %u:", b3host::native_chain::t_depth);
    for (unsigned k = 0; k < b3host::native_chain::t_depth && k < b3host::native_chain::kMax; ++k)
        std::fprintf(stdout, "%s %08X %s", k ? " >" : "", b3host::native_chain::t_chain[k],
                     b3host::describe_function(b3host::native_chain::t_chain[k]).c_str());
    std::fprintf(stdout, "\nwatchdog: %llu virtual VBlanks\n", (unsigned long long)b3native::platform::vblank_count());
    if (mini::R5900* m = b3native::active_mini().load())   // vu0: where retail code in the mini is
        std::fprintf(stdout, "watchdog: mini pc %08X %s ra %08X %s; VU0 launches %llu\n", m->pc(),
                     b3host::describe_function(m->pc()).c_str(), static_cast<std::uint32_t>(m->gpr_lo(31)),
                     b3host::describe_function(static_cast<std::uint32_t>(m->gpr_lo(31))).c_str(),
                     (unsigned long long)b3native::platform::vu0_micro_calls());
    {
        const auto& ms = b3native::all_minis();
        for (std::size_t k = 0; k < ms.size() && k < 64; ++k)
            std::fprintf(stdout, "watchdog: mini %zu pc %08X %s ra %08X %s sp %08X\n", k, ms[k]->pc(),
                         b3host::describe_function(ms[k]->pc()).c_str(), static_cast<std::uint32_t>(ms[k]->gpr_lo(31)),
                         b3host::describe_function(static_cast<std::uint32_t>(ms[k]->gpr_lo(31))).c_str(),
                         static_cast<std::uint32_t>(ms[k]->gpr_lo(29)));
    }
    auto& nb = static_cast<b3host::NativeBackend&>(b3host::backend());
    auto& hw = b3native::platform::hw_model();
    auto rd8 = [&](std::uint32_t a) { return static_cast<unsigned>(nb.ram()[a]); };
    auto rd32 = [&](std::uint32_t a) { std::uint32_t v; std::memcpy(&v, nb.ram().data() + a, 4); return v; };
    std::fprintf(stdout, "watchdog: EIE %d D_STAT %08X INTC_STAT %08X DMAs %llu; rwDMAPktBase %u, _rwDMAAsyncPktPtr[0..3] %u %u %u %u, "
                         "rwDMADispatchPtr[0..1] %u %u, rwDMADispatching %08X rwDMAFlipSt %08X, D1_CHCR %08X D2_CHCR %08X\n",
                 nb.interrupts_enabled() ? 1 : 0, hw.d_stat(), hw.intc_stat(), (unsigned long long)hw.dma_count(),
                 rd8(0x4E2830), rd8(0x4E27CC), rd8(0x4E27CD), rd8(0x4E27CE), rd8(0x4E27CF), rd8(0x4E2840), rd8(0x4E2841),
                 rd32(0x4E2838), rd32(0x4E283C), hw.chan(1, 0), hw.chan(2, 0));
    std::fprintf(stdout, "watchdog: GIF FQC latched %d, M3P %d; D1 MADR %08X QWC %08X TADR %08X; D2 MADR %08X TADR %08X\n",
                 hw.fqc_latched() ? 1 : 0, hw.mskpath3() ? 1 : 0, hw.chan(1, 1), hw.chan(1, 2), hw.chan(1, 3), hw.chan(2, 1),
                 hw.chan(2, 3));
    if (std::getenv("B3NATIVE_ENTERED")) report_entered();   // vu0: the call counts of a run that never ends
    if (std::getenv("B3NATIVE_PCCOUNT"))   // vu0: the mini's pc counters of a run that never ends
        for (const auto& [pc, n] : *b3native::pc_counts())
            std::printf("pccount: %08X %s %llu\n", pc, b3host::describe_function(pc).c_str(), (unsigned long long)n);
    std::fflush(stdout);
    _exit(3);
}

// boot2: a crash prints the native chain and the host backtrace (on an alternate stack: a stack overflow too)
void on_crash(int sig) {
    if (b3host::detail::t_any_foreign)   // boot2: a foreign pointer (host.h) was formed on this thread; an access faults
        std::fprintf(stdout, "crash: the last foreign guest pointer formed on this thread: %08X (outside RAM/scratchpad)\n",
                     b3host::detail::t_last_foreign);
    std::fprintf(stdout, "crash: signal %d; native chain depth %u:", sig, b3host::native_chain::t_depth);
    for (unsigned k = 0; k < b3host::native_chain::t_depth && k < b3host::native_chain::kMax; ++k)
        std::fprintf(stdout, " %08X", b3host::native_chain::t_chain[k]);
    std::fprintf(stdout, "\n");
    std::fflush(stdout);
#ifdef _WIN32
    void* bt[62];
    const unsigned short n = CaptureStackBackTrace(0, 62, bt, nullptr);
    const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleA(nullptr));
    for (unsigned short k = 0; k < n; ++k)   // image offsets: addr2line -e native_boot.exe 0x140000000+off
        std::fprintf(stdout, "  #%u +0x%llx\n", k, static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(bt[k]) - base));
    std::fflush(stdout);
    _exit(4);
#else
    void* bt[64];
    const int n = backtrace(bt, 64);
    backtrace_symbols_fd(bt, n, 1);
    _exit(4);
#endif
}
#ifdef _WIN32
LONG WINAPI on_crash_seh(EXCEPTION_POINTERS* e) {   // win1: SIGSEGV's counterpart (access violation, stack overflow, ...)
    std::fprintf(stdout, "crash: exception %08lX at %p\n", e->ExceptionRecord->ExceptionCode, e->ExceptionRecord->ExceptionAddress);
    on_crash(SIGSEGV);
    return EXCEPTION_EXECUTE_HANDLER;
}
#endif

int main(int argc, char** argv) {
    bool skip_lib = false, sinit = false, static_init = false, disc_args = false;
    bool poll_on = false;   // --poll-point
    std::string poll_pads;
    std::uint64_t poll_max = 0;
    b3host::gaddr entry = 0;
    b3gv_native::Options render;   // pic1: --render
    render.vblank = b3native::platform::vblank_count;
    for (int k = 1; k < argc; ++k) {
        const std::string s = argv[k];
        if (s == "--skip-crt0-lib") skip_lib = true;
        else if (s == "--sinit") sinit = true;
        else if (s == "--static-init") static_init = true;
        else if (s == "--disc-args") disc_args = true;
        else if (s == "--entry" && k + 1 < argc) entry = static_cast<b3host::gaddr>(std::strtoul(argv[++k], nullptr, 16));
        else if (s == "--oracle" && k + 1 < argc) {
            Oracle o;
            char* end = nullptr;
            o.target = static_cast<b3host::gaddr>(std::strtoul(argv[++k], &end, 16));
            if (end && *end == ':') o.want = static_cast<unsigned>(std::strtoul(end + 1, nullptr, 10));
            g_oracles.push_back(o);
        } else if (s == "--poll-point") {
            poll_on = true;
        } else if (s == "--pads" && k + 1 < argc) {
            poll_pads = argv[++k];
        } else if (s == "--movie-log") {   // movie2
            g_movie.on = true;
            if (k + 1 < argc && std::isdigit(static_cast<unsigned char>(argv[k + 1][0]))) g_movie.ends_wanted = static_cast<unsigned>(std::strtoul(argv[++k], nullptr, 10));
        } else if (s == "--polls" && k + 1 < argc) {
            poll_max = std::strtoull(argv[++k], nullptr, 10);
            poll_on = true;
        } else if (s == "--render" && k + 1 < argc) {
            render.mode = argv[++k];
        } else if (s == "--render-lib" && k + 1 < argc) {
            render.lib = argv[++k];
        } else if (s == "--render-args" && k + 1 < argc) {
            render.args = argv[++k];
        } else if (s == "--lookahead" && k + 1 < argc) {
            char* end = nullptr;
            g_look.target = static_cast<b3host::gaddr>(std::strtoul(argv[++k], &end, 16));
            if (end && *end == ':') g_look.want = static_cast<unsigned>(std::strtoul(end + 1, nullptr, 10));
        }
    }
    std::setvbuf(stdout, nullptr, _IOLBF, 0);   // boot2: lines survive a timeout
    {
#ifdef _WIN32
        ULONG guarantee = 64 * 1024;   // room for the report after a stack overflow
        SetThreadStackGuarantee(&guarantee);
        SetUnhandledExceptionFilter(on_crash_seh);
        std::signal(SIGABRT, on_crash);   // boot2: B3HOST_FATAL_ABORT
#else
        static std::vector<char> alt(1 << 20);
        stack_t ss{};
        ss.ss_sp = alt.data();
        ss.ss_size = alt.size();
        sigaltstack(&ss, nullptr);
        struct sigaction sa{};
        sa.sa_handler = on_crash;
        sa.sa_flags = SA_ONSTACK;
        sigaction(SIGSEGV, &sa, nullptr);
        sigaction(SIGBUS, &sa, nullptr);
        sigaction(SIGABRT, &sa, nullptr);   // boot2: B3HOST_FATAL_ABORT
#endif
    }
    if (const char* w = std::getenv("B3NATIVE_WATCHDOG")) {
#ifdef _WIN32   // win1: no SIGALRM; a thread that reports after the time (watchdog() ends the process)
        const unsigned secs = static_cast<unsigned>(std::strtoul(w, nullptr, 10));
        std::thread([secs] { std::this_thread::sleep_for(std::chrono::seconds(secs)); watchdog(0); }).detach();
#else
        std::signal(SIGALRM, watchdog);
        alarm(static_cast<unsigned>(std::strtoul(w, nullptr, 10)));
#endif
    }
    const char* env = std::getenv("B3_ELF");
    // (a developer tool: the repo's data/game when it is there, else this PC's perf install)
    const std::string elf = env && *env ? env : std::filesystem::exists("data/game/SLUS_210.50") ? "data/game/SLUS_210.50" : kDefaultElf;
    g_elf = elf;
    // the disc's other files beside it, unless B3_DISC_ROOT says otherwise (platform_sif.cpp disc_root's default is
    // ./data/game only)
    // (and the game view's renderer, gameview_native.cpp, its disc folder from B3_GAME_DIR the same way)
    for (const char* var : {"B3_DISC_ROOT", "B3_GAME_DIR"})
        if (const char* dr = std::getenv(var); !(dr && *dr)) {
            const std::size_t cut = elf.find_last_of("/\\");
            const std::string root = cut == std::string::npos ? std::string(".") : elf.substr(0, cut);
#ifdef _WIN32
            _putenv_s(var, root.c_str());
#else
            setenv(var, root.c_str(), 1);
#endif
        }
    if (!poll_pads.empty() && !poll_on) { std::printf("--pads: only with --poll-point\n"); return 2; }
    if (poll_on) {
        if (!g_oracles.empty() || g_look.target) { std::printf("--poll-point: not with --oracle / --lookahead\n"); return 2; }
        if (!poll_setup(poll_pads, poll_max)) return 2;
        b3host::g_oracle_hook = oracle_hook;
        b3host::add_oracle_target(kUpdatePort);
    }
    if (g_movie.on) {
        if (poll_on || !g_oracles.empty() || g_look.target) { std::printf("--movie-log: not with --poll-point / --oracle / --lookahead (one hooked function per run)\n"); return 2; }
        b3host::g_oracle_hook = oracle_hook;
        b3host::g_oracle_target = kMpegIsEnd;
    }
    if (g_look.target) {
        if (!g_oracles.empty()) { std::printf("--lookahead and --oracle: one per run\n"); return 2; }
        b3host::g_oracle_hook = oracle_hook;
        b3host::add_oracle_target(g_look.target);
    }
    if (!g_oracles.empty()) {
        b3host::g_oracle_hook = oracle_hook;
        b3host::add_oracle_target(g_oracles.front().target);
        if (g_oracles.size() > 1) { std::printf("--oracle: one function per run\n"); return 2; }
    }
    if (!render.mode.empty()) {   // pic1: the game view (before the first guest call)
        const std::string self = argv[0];
        const std::size_t cut = self.find_last_of("/\\");
        if (!b3gv_native::install(render, cut == std::string::npos ? std::string(".") : self.substr(0, cut))) return 2;
    }
    std::size_t n = 0;
    b3native::install(n);
    auto nb = std::make_unique<b3host::NativeBackend>();
    if (!load(*nb, elf)) return 2;
    b3native::platform::install_hw_model(*nb);
    b3native::platform::install_rwa_iop();   // boot2: RW Audio IOP executor (SIF command 0)
    b3host::BackendScope scope(*nb);
    nb->set_stack_pointer(kStackTop);
    std::printf("native_boot: %zu native functions in the table; ELF %s\n", n, elf.c_str());
    // native3 (Step 0): a guest call with no native function runs retail in the mini (tools/native/native_fallback.h);
    // B3NATIVE_STRICT=1 keeps the halt (MissingNative), bit for bit as before
    std::unique_ptr<b3native::NativeFallback> fallback;
    if (const char* st = std::getenv("B3NATIVE_STRICT"); !(st && std::string(st) == "1")) {
        fallback = std::make_unique<b3native::NativeFallback>(*nb);
        fallback->install_into_backend();
        // boot2 (gen1's option A): a static initialiser (0x4D3E00-0x4DD820) that runs in the fallback (make native
        // NATIVE_SINIT_RETAIL=1) runs with its whole call tree retail -- every game / library native switched off for
        // the call, restored after -- so the initialisers' stale stack lanes are left by retail callees (constructors,
        // GtMath helpers), not by native ones' missing frames and bounce windows (gen1: 21 stale words otherwise). Keyed
        // on the initialisers themselves: their walker func_00100240 is a platform entry (gen1's catch)
        if (!b3host::native_override(0x004D3E00u)) {
            auto inner = nb->fallback;
            b3native::NativeFallback* fb = fallback.get();
            nb->fallback = [inner, fb](b3host::gaddr f, const b3host::CallArgs& a, b3host::CallResult& r) {
                if (f < 0x004D3E00u || f >= 0x004DD820u) { inner(f, a, r); return; }
                std::size_t n = 0;
                const b3native::TableEntry* e = b3native::install(n);
                std::vector<std::tuple<const b3host::NativeOverride*, bool, b3host::gaddr>> saved;
                for (std::size_t k = 0; k < n; ++k) {
                    // sync1: the platform's natives too (memset, malloc ...): a host version pushes no guest frame, so
                    // a stack slot retail's callee frame overwrites kept an earlier initialiser's float (gen1's 17 lanes:
                    // --static-init then DIFF 20 words; with them retail 3, the kernel objects). The 158 initialisers
                    // reach no syscall the mini cannot run.
                    if (const b3host::NativeOverride* o = b3host::native_override(e[k].address)) {
                        saved.emplace_back(o, o->enabled->load(), e[k].address);
                        o->enabled->store(false);
                        fb->hook_off(e[k].address);   // sync1: the mini runs it retail (else MissingNative at 0x130BF0)
                    }
                }
                struct Restore {
                    std::vector<std::tuple<const b3host::NativeOverride*, bool, b3host::gaddr>>& v;
                    b3native::NativeFallback* fb;
                    ~Restore() {
                        for (auto& [o, on, ad] : v) {
                            o->enabled->store(on);
                            fb->hook_on(ad);
                        }
                    }
                } restore{saved, fb};
                inner(f, a, r);
            };
            std::printf("native_boot: the static initialisers (0x4D3E00-0x4DD820) run retail with their whole call tree\n");
        }
        std::printf("native_boot: fallback ON (functions without a native version run retail in the mini; "
                    "B3NATIVE_STRICT=1 halts instead)\n");
    } else {
        std::printf("native_boot: B3NATIVE_STRICT=1: no fallback (a missing function ends the run)\n");
    }

    vu0bw::install();

    if (entry) {
        const Step s = call(*nb, "entry " + hex32(entry) + " " + b3host::describe_function(entry), entry, args_of({}));
        std::printf("%s: %s\n", s.what.c_str(), s.outcome.c_str());
        return s.ok ? 0 : 1;
    }
    // play1: exploration only -- the movie stand-in (tools/native/explore_standins.h); never with a proof
    std::unique_ptr<b3native::MovieStandIn> movie_standin;
    if (b3native::MovieStandIn::requested()) {
        if (poll_on || !g_oracles.empty() || g_look.target) {
            std::printf("B3NATIVE_MOVIE_STANDIN: exploration only, refused with --poll-point / --oracle / --lookahead\n");
            return 2;
        }
        movie_standin = std::make_unique<b3native::MovieStandIn>();
        if (!movie_standin->install(*nb)) {
            std::printf("B3NATIVE_MOVIE_STANDIN: needs the fallback (not with B3NATIVE_STRICT=1)\n");
            return 2;
        }
    }

    // crt0, as host code: the library calls, then main, then exit
    std::vector<Step> steps;
    auto step = [&](const std::string& what, b3host::gaddr fn, b3host::CallArgs a, bool library) {
        if (library && skip_lib) { steps.push_back({what, "skipped (--skip-crt0-lib)", true}); return true; }
        steps.push_back(call(*nb, what, fn, a));
        return steps.back().ok;
    };
    bool ok = step("_InitSys", 0x00115108u, args_of({}), true) &&
              step("FlushCache(0)", 0x0010C0A0u, args_of({0}), true);
    if (ok && static_init) {
        nb->set_stack_pointer(kStackTop - kMainFrame);   // boot2: inside main's frame, as retail calls it
        ok = step("static init = func_00100240 (main's first call)", 0x00100240u, args_of({}), false);
        if (ok) {   // boot1: the whole sequence in retail order against retail's func_00100240 in the mini
            auto cpu = std::make_unique<mini::R5900>();
            if (cpu->load_elf(elf.c_str())) {
                cpu->set_gpr64(29, kStackTop - kMainFrame);
                cpu->set_gpr64(28, 0x004E8670u);
                try {
                    cpu->run_from(0x00100240u);
                    unsigned nd = 0, written = 0;
                    std::string first;
                    auto fresh = std::make_unique<mini::R5900>();
                    fresh->load_elf(elf.c_str());
                    for (std::uint32_t a = 0; a + 4 <= 0x01FF0000u; a += 4) {
                        if (std::memcmp(cpu->ram().data() + a, nb->ram().data() + a, 4) != 0) {
                            if (nd++ == 0) first = hex32(a);
                            if (std::getenv("B3NATIVE_DIFFS") && nd <= 200) {
                                std::uint32_t rv, nv;
                                std::memcpy(&rv, cpu->ram().data() + a, 4);
                                std::memcpy(&nv, nb->ram().data() + a, 4);
                                std::printf("  diff %s retail %08X native %08X\n", hex32(a).c_str(), rv, nv);
                            }
                        }
                        if (std::memcmp(cpu->ram().data() + a, fresh->ram().data() + a, 4) != 0) ++written;
                    }
                    std::printf("static init vs retail (all 158 in order, RAM below the stack): %s\n",
                                nd ? ("DIFF " + std::to_string(nd) + " words, first " + first).c_str()
                                   : ("SAME (" + std::to_string(written) + " words written by retail)").c_str());
                } catch (const std::exception& e) {
                    std::printf("static init vs retail: retail run failed in the mini: %s\n", e.what());
                }
            }
        }
    } else if (ok) {
        if (disc_args) setup_boot_args(elf);
        else b3native::platform::stub_note("SetupThread", "boot arguments: argc 0, argv[0] NULL (the game's boot path "
                                                          "becomes \"cdrom0:\\(NULL);1\"); --disc-args for a disc boot");
        const std::uint32_t argc_word = b3host::load<std::uint32_t>(0x01D9CE80u);
        // boot2: native main runs with retail's sp inside its frame (kMainFrame); a retail main (the fallback) makes
        // its own frame, so only when main is native
        const bool main_native = b3host::native_override(0x0012EB30u) != nullptr;
        if (main_native) nb->set_stack_pointer(kStackTop - kMainFrame);
        ok = step("main = func_0012EB30(argc, argv)", 0x0012EB30u, args_of({argc_word, 0x01D9CE84u}), false);
    }
    if (ok) step("exit", 0x001213B8u, args_of({0}), true);
    for (const Step& s : steps) std::printf("boot: %s: %s\n", s.what.c_str(), s.outcome.c_str());
    for (const Oracle& o : g_oracles)
        if (!o.done) std::printf("oracle %s: %s\n", hex32(o.target).c_str(), o.armed ? "the call did not return (the boot stopped inside it)" : "not reached");
    if (g_stop) std::printf("stopped at %s %s: %s\n", hex32(g_stop).c_str(), b3host::describe_function(g_stop).c_str(),
                            why_missing(g_stop).c_str());
    report_entered();
    b3gv_native::finish();   // pic1: the renderer drains the feed, writes its shots
    for (const auto& e : b3native::platform::stub_log())
        std::printf("stub log: %s (%u call%s): %s\n", e.stub, e.calls, e.calls == 1 ? "" : "s", e.effect.c_str());
    {   // boot2: ptr_arg bounce windows per host site (a to-do list for T31 / T27; resolve with addr2line -f -C -i)
        const auto sites = b3host::detail::bounce_sites();
        std::uint64_t total = 0;
        for (const auto& b : sites) total += b.opens;
        std::printf("bounce windows: %llu opened at %zu host sites (host memory copied onto the guest stack)\n",
                    static_cast<unsigned long long>(total), sites.size());
#ifdef _WIN32
        const auto image_base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));   // win1's guards: windows.h
#else
        Dl_info self{};
        dladdr(reinterpret_cast<void*>(&on_crash), &self);
        const auto image_base = reinterpret_cast<std::uintptr_t>(self.dli_fbase);
#endif
        for (std::size_t k = 0; k < sites.size() && k < 40; ++k) {
            const auto off = reinterpret_cast<std::uintptr_t>(sites[k].site) - image_base;
            std::printf("bounce window site: +0x%llx %llu opens, up to %u bytes\n", static_cast<unsigned long long>(off),
                        static_cast<unsigned long long>(sites[k].opens), sites[k].max_bytes);
        }
    }
    if (fallback) {
        fallback->report(stdout);
        if (movie_standin) movie_standin->report(stdout);
        if (fallback->total_calls() && g_poll.on)   // boot2's condition: a poll-point run with fallbacks says so
            std::printf("poll: NOTE fallbacks active (%zu functions ran retail in the mini, %llu calls); costs differ from "
                        "the static model\n", fallback->log().size(), static_cast<unsigned long long>(fallback->total_calls()));
    }

    if (sinit) {
        // each static initialiser alone, on the fresh image (they are independent in retail only through the order;
        // this counts which can run natively at all)
        unsigned total = 0, okc = 0, same = 0, diff = 0, noracle = 0;
        auto cpu = std::make_unique<mini::R5900>();
        const bool oracle = cpu->load_elf(elf.c_str());
        const std::vector<std::uint8_t> image = cpu->ram();
        for (b3host::gaddr p = kCtorBegin; p < kCtorEnd; p += 4) {
            if (!load(*nb, elf)) return 2;
            nb->set_stack_pointer(kStackTop);
            const b3host::gaddr f = b3host::load<std::uint32_t>(p);
            Step s = call(*nb, "sinit " + hex32(f) + " " + b3host::describe_function(f), f, args_of({}));
            ++total;
            if (s.ok) {
                ++okc;
                std::string verdict = "ORACLE n/a";
                if (oracle) {
                    cpu->ram() = image;
                    cpu->set_gpr64(29, kStackTop);
                    cpu->set_gpr64(28, 0x004E8670u);   // gp as crt0 sets it [C crt0.s 0x1001C0]
                    try {
                        cpu->run_from(f);
                        unsigned nd = 0;
                        std::string first;
                        for (std::uint32_t a = 0; a + 4 <= 0x01FF0000u; a += 4)
                            if (std::memcmp(cpu->ram().data() + a, nb->ram().data() + a, 4) != 0 && nd++ == 0) first = hex32(a);
                        unsigned written = 0;   // words retail changed: the comparison is not of untouched memory
                        for (std::uint32_t a = 0; a + 4 <= 0x01FF0000u; a += 4)
                            if (std::memcmp(cpu->ram().data() + a, image.data() + a, 4) != 0) ++written;
                        verdict = nd ? "DIFF " + std::to_string(nd) + " words, first " + first
                                     : "SAME as retail (" + std::to_string(written) + " words written)";
                        (nd ? diff : same) += 1;
                    } catch (const std::exception& e) {
                        verdict = std::string("ORACLE retail run failed: ") + e.what();
                        ++noracle;
                    }
                }
                s.outcome += "; " + verdict;
            }
            std::printf("%s: %s\n", s.what.c_str(), s.outcome.substr(0, 300).c_str());
        }
        std::printf("static initialisers: %u of %u complete natively; vs retail: %u same, %u differ, %u no oracle\n", okc,
                    total, same, diff, noracle);
    }
    return ok ? 0 : 1;
}
