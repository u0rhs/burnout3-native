// The native backend's hand-written tier (src/native/platform.h).
#include "platform.h"

#include "b3host/call.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace b3native::platform {

namespace {
std::vector<StubEvent> g_log;
struct LogAtExit {
    ~LogAtExit() {
        const char* path = std::getenv("B3NATIVE_STUBLOG");
        if (!path || !*path || g_log.empty()) return;
        if (std::FILE* f = std::fopen(path, "a")) {
            for (const StubEvent& e : g_log) std::fprintf(f, "%s\t%u\t%s\n", e.stub, e.calls, e.effect.c_str());
            std::fclose(f);
        }
    }
} g_log_at_exit;
}  // namespace

void stub_note(const char* stub, const std::string& effect) {
    for (StubEvent& e : g_log)
        if (std::strcmp(e.stub, stub) == 0 && e.effect == effect) { ++e.calls; return; }
    g_log.push_back({stub, effect, 1});
}
const std::vector<StubEvent>& stub_log() { return g_log; }
void stub_unexpected(const char* stub, const std::string& what) {
    b3host::fatal(std::string("platform stub ") + stub + ": unexpected " + what + " (not modelled; retail would differ)");
}

// MWCC's __initialize_cpp_rts(ctor_begin, ctor_end, eti_begin, eti_end) [C decomp mwrt.s 0x100810]: calls every word
// of [ctor_begin, ctor_end) in order (jalr, no arguments), i.e. the static initialisers in retail link order. The
// table is guest data (the ELF image), so retail's order is kept by construction. eti (exception tables) unused.
void initialize_cpp_rts(const b3host::CallArgs& a, b3host::CallResult& r) {
    const b3host::gaddr begin = a.gpr[0].w[0], end = a.gpr[1].w[0];
    for (b3host::gaddr p = begin; p < end; p += 4) b3host::call_guest<void>(b3host::load<std::uint32_t>(p));
    r = b3host::CallResult{};
}

// crt0's func_00100240 [C decomp crt0.s]: __initialize_cpp_rts(0x4DD820, 0x4DDA98, 0x4E0610, 0x4E0610) as a tail call.
void crt0_init_cpp(const b3host::CallArgs& a, b3host::CallResult& r) {
    b3host::CallArgs c = a;
    c.gpr[0] = b3host::detail::gpr64(0x004DD820u);
    c.gpr[1] = b3host::detail::gpr64(0x004DDA98u);
    c.gpr[2] = b3host::detail::gpr64(0x004E0610u);
    c.gpr[3] = b3host::detail::gpr64(0x004E0610u);
    initialize_cpp_rts(c, r);
}

// FlushCache (syscall 0x64): write back / invalidate the EE caches. Native memory is coherent: nothing to do. Modes
// 0 (data cache write-back) and 2 (instruction cache invalidate) [C SDK libkernl FlushCache(int)]; anything else is
// fatal. v0 is whatever the kernel returns [L: retail's callers do not read v0 after `jal FlushCache`].
void flush_cache(const b3host::CallArgs& a, b3host::CallResult&) {
    const std::uint32_t mode = a.gpr[0].w[0];
    if (mode != 0 && mode != 2) stub_unexpected("FlushCache", "mode " + std::to_string(mode));
}

// _InitSys [C decomp libkernl.s 0x115108]: libkernl's start-up from crt0: supplement_crt0, the system call table,
// InitTimer / StartTimerSystemTime (the timer interrupt and alarm lists), InitThread, InitExecPS2, InitTLBFunctions.
// Natively none of that hardware or kernel exists: threads, timers and alarms are the S5 frame scheduler's. A no-op
// that leaves libkernl's own data (.data 0x484140-0x485840, .bss 0x4E3240-0x4EAF40 [C decomp yaml]) as the image left
// it, where retail fills its timer, alarm and thread tables. Game code reads that state only through libkernl
// functions, each a platform-tier entry or MISSING [L]. Logged.
void init_sys(const b3host::CallArgs&, b3host::CallResult&) {
    // boot2 (2026-11-07, movie2's kernel-object-id finding: a recorded retail run's semaphore and thread ids were 1-3 above
    // ours): the kernel objects _InitSys creates, in retail's order, through the platform's own kernel calls, the
    // parameter blocks on the guest stack as retail fills them (the unwritten words as the stack holds them):
    //   supplement_crt0 [C 0x1151D0..]: two semaphores {max 1, init 1} -> __sce_sema_id (0x484890), __sce_eh_sema_id;
    //   InitThread [C 0x10CBF8..]: when topId <= 0, a semaphore {max 0xFF, init 0} -> D_004E3640, the top thread
    //   {entry topThread 0x10CB20, stack 0x4E3240, 0x400 bytes, gp 0x4E8670, priority 0} -> topId (0x484158),
    //   D_004E3648 = 0, +4 = 0, StartThread(topId, &D_004E3648), then the current thread's priority 1.
    // [C navproof14s1 poll 1: semaphores 0 / 1 (1/1), 2 (0/255, thread 2 waiting), thread 2 = topThread]
    auto call = [](void (*fn)(const b3host::CallArgs&, b3host::CallResult&), std::initializer_list<std::uint32_t> v) {
        b3host::CallArgs a{};
        unsigned i = 0;
        for (const std::uint32_t x : v) a.gpr[i++].w[0] = x;
        a.n_gpr = i;
        a.sp = b3host::backend().stack_pointer();
        b3host::CallResult r{};
        fn(a, r);
        return static_cast<std::int32_t>(r.v0.w[0]);
    };
    {
        b3host::GuestStackFrame f(0x50);
        const b3host::gaddr p = f.addr();
        b3host::store<std::uint32_t>(p + 0x04, 1u);
        b3host::store<std::uint32_t>(p + 0x08, 1u);
        b3host::store<std::uint32_t>(p + 0x24, 1u);
        b3host::store<std::uint32_t>(p + 0x28, 1u);
        b3host::store<std::uint32_t>(0x00484890u, static_cast<std::uint32_t>(call(create_sema, {p})));
        b3host::store<std::uint32_t>(0x00484894u, static_cast<std::uint32_t>(call(create_sema, {p + 0x20})));
    }
    if (static_cast<std::int32_t>(b3host::load<std::uint32_t>(0x00484158u)) <= 0) {
        b3host::GuestStackFrame f(0x80);
        const b3host::gaddr p = f.addr();
        b3host::store<std::uint32_t>(p + 0x34, 0xFFu);
        b3host::store<std::uint32_t>(p + 0x38, 0u);
        const std::int32_t sema = call(create_sema, {p + 0x30});
        b3host::store<std::uint32_t>(0x004E3640u, static_cast<std::uint32_t>(sema));
        if (sema >= 0) {
            b3host::store<std::uint32_t>(p + 0x04, 0x0010CB20u);
            b3host::store<std::uint32_t>(p + 0x08, 0x004E3240u);
            b3host::store<std::uint32_t>(p + 0x0C, 0x400u);
            b3host::store<std::uint32_t>(p + 0x10, 0x004E8670u);
            b3host::store<std::uint32_t>(p + 0x14, 0u);
            const std::int32_t id = call(create_thread, {p});
            b3host::store<std::uint32_t>(0x00484158u, static_cast<std::uint32_t>(id));
            if (id >= 0) {
                b3host::store<std::uint32_t>(0x004E3648u, 0u);
                b3host::store<std::uint32_t>(0x004E364Cu, 0u);
                call(start_thread, {static_cast<std::uint32_t>(id), 0x004E3648u});
                call(change_thread_priority, {static_cast<std::uint32_t>(call(get_thread_id, {})), 1u});
            }
        }
    }
    note_libkernl_timer_handler();   // boot1: InitTimer's AddIntcHandler2(TIM3) takes INTC id 1, as in retail
    stub_note("_InitSys", "libkernl's semaphores and top thread made as retail's (supplement_crt0, InitThread); its "
                          "other data (timer, alarm, TLB tables) not initialised");
}

// exit(code) [C decomp libc.s 0x1213B8]: runs the atexit handlers (_impure_ptr->_atexit, +0x148), the cleanup hook
// (+0x3C), then _exit. Natively: if any atexit handler or cleanup hook is registered, retail would run guest code
// here: fatal (not modelled). Otherwise the program ends: NativeExit to the tool that started it.
void exit_(const b3host::CallArgs& a, b3host::CallResult&) {
    const b3host::gaddr impure = b3host::load<std::uint32_t>(0x004876E8u);   // _impure_ptr [C symbol_addrs]
    if (impure && b3host::load<std::uint32_t>(impure + 0x148)) stub_unexpected("exit", "registered atexit handlers");
    if (impure && b3host::load<std::uint32_t>(impure + 0x3C)) stub_unexpected("exit", "a registered cleanup hook");
    throw NativeExit(static_cast<int>(a.gpr[0].w[0]));
}

// fe1: void LoadExecPS2(const char *filename, int argc, char *argv[]) [C decomp sce/libkernl.s func_001154C0: two libkernl
// calls, then the _LoadExecPS2 syscall; never returns]. The game's one caller is func_001D3D70 (main's tail when
// D_0051BAD7 is set: the EXTRAS page's PLAY NFS:UNDERGROUND 2 DEMO, "cdrom0:\NFSUNDER\NFSUNDER.ELF;1" with
// "language=<n>" and "exe=cdrom0:\SLUS_210.50;1"), after it has shut the game down (the dev9 power-off, sceCdInit(5),
// sceSifExitCmd). The PS2 would replace Burnout 3 with that program; the port cannot run it, so the program ends here,
// as at exit(0), with the request logged.
void load_exec_ps2(const b3host::CallArgs& a, b3host::CallResult&) {
    const auto str = [](b3host::gaddr p) {
        std::string s;
        for (; p && s.size() < 256; ++p) {
            const char c = static_cast<char>(b3host::load<std::uint8_t>(p));
            if (!c) break;
            s.push_back(c);
        }
        return s;
    };
    const std::int32_t argc = static_cast<std::int32_t>(a.gpr[1].w[0]);
    const b3host::gaddr argv = a.gpr[2].w[0];
    std::string args;
    for (std::int32_t k = 0; argv && k < argc && k < 8; ++k) args += (k ? ", \"" : "\"") + str(b3host::load<std::uint32_t>(argv + 4u * k)) + "\"";
    std::printf("platform: LoadExecPS2 \"%s\" (%d args: %s): the game hands over to another program; the port ends here\n",
                str(a.gpr[0].w[0]).c_str(), argc, args.c_str());
    std::fflush(stdout);
    throw NativeExit(0);
}

}  // namespace b3native::platform
