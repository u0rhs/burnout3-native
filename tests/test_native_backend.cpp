// The native backend (S0, docs/standalone.md §8): arena, ELF image, the strict missing/fault rules, the native call
// chain, static init through the ctor table. Synthetic data only (no disc data).
#include "b3host/call.h"
#include "b3host/native_backend.h"
#include "test.h"

#include <cstdio>
#include <fstream>
#include <vector>
#ifndef _WIN32
#include <csignal>
#include <sys/wait.h>
#include <unistd.h>
#include "../tools/native/crash_report.h"   // the crash report's "guest memory overrun" line
#endif

using namespace b3host;

namespace {

const FunctionInfo kInfo[] = {
    {0x00100000u, 0x10u, "game", "matched", "leaf_a"},
    {0x00100010u, 0x10u, "game", "draft", "calls_missing"},
    {0x00100020u, 0x10u, "game", "none", "missing_one"},
    {0x00100030u, 0x10u, "sce", "none", "ctor_a"},
    {0x00100040u, 0x10u, "sce", "none", "ctor_b"},
};

int g_order[4];
int g_n = 0;
int leaf_a(int x) { return x * 3; }
void calls_missing() { call_guest<void>(0x00100020u); }
void ctor_a() { g_order[g_n++] = 1; }
void ctor_b() { g_order[g_n++] = 2; }
void ctor_walk(const CallArgs& a, CallResult&) {   // as the platform tier's __initialize_cpp_rts
    for (gaddr p = a.gpr[0].w[0]; p < a.gpr[1].w[0]; p += 4) call_guest<void>(load<std::uint32_t>(p));
}
void mmio_reader() { (void)load<std::uint32_t>(0x10003000u); }

std::atomic<bool> g_on{true};
std::atomic<std::uint64_t> g_nested{0};
const NativeOverride kTable[] = {
    {0x00100000u, &traced_entry<0x00100000u, &native_entry<&leaf_a>>, 4, &g_on, &g_nested},
    {0x00100010u, &traced_entry<0x00100010u, &native_entry<&calls_missing>>, 4, &g_on, &g_nested},
    {0x00100030u, &traced_entry<0x00100030u, &native_entry<&ctor_a>>, 4, &g_on, &g_nested},
    {0x00100040u, &traced_entry<0x00100040u, &native_entry<&ctor_b>>, 4, &g_on, &g_nested},
    {0x00100050u, &traced_entry<0x00100050u, &ctor_walk>, 4, &g_on, &g_nested},
    {0x00100060u, &traced_entry<0x00100060u, &native_entry<&mmio_reader>>, 4, &g_on, &g_nested},
};

CallArgs args(std::uint32_t a0 = 0, std::uint32_t a1 = 0) {
    CallArgs c;
    c.gpr[0] = detail::gpr64(a0);
    c.gpr[1] = detail::gpr64(a1);
    c.n_gpr = c.n_fpr = 8;
    c.sp = 0x01F00000u;
    return c;
}

struct Setup {
    NativeBackend nb;
    BackendScope scope{nb};
    Setup() {
        set_native_overrides(kTable, sizeof kTable / sizeof kTable[0]);
        set_function_info(kInfo, sizeof kInfo / sizeof kInfo[0]);
        nb.set_stack_pointer(0x01F00000u);
    }
    ~Setup() { set_native_overrides(nullptr, 0); set_function_info(nullptr, 0); }
};

}  // namespace

TEST(runs_native_and_keeps_unset_results) {
    Setup s;
    CallResult r;
    r.f0 = 0x12345678u;
    s.nb.run(0x00100000u, args(7), r);
    CHECK(r.v0.w[0] == 21u);
    CHECK(r.f0 == 0x12345678u);   // an int function leaves f0 alone
    CHECK(s.nb.stack_pointer() == 0x01F00000u);
}

TEST(missing_callee_is_fatal_with_chain) {
    Setup s;
    CallResult r;
    bool caught = false;
    try {
        s.nb.run(0x00100010u, args(), r);
    } catch (const MissingNative& e) {
        caught = true;
        CHECK(e.address == 0x00100020u);
        CHECK(e.chain.size() == 1 && e.chain[0] == 0x00100010u);
        CHECK(std::string(e.what()).find("missing_one [game, none]") != std::string::npos);
    }
    CHECK(caught);
    CHECK(native_chain::t_depth == 0);   // unwound
    CHECK(s.nb.stack_pointer() == 0x01F00000u);
    CHECK_THROWS(s.nb.run(0x00100020u, args(), r));   // the entry itself missing
}

TEST(mmio_is_fatal) {
    Setup s;
    CallResult r;
    bool caught = false;
    try {
        s.nb.run(0x00100060u, args(), r);
    } catch (const NativeFault& e) {
        caught = true;
        CHECK(e.address == 0x10003000u && e.len == 4 && !e.write);
    }
    CHECK(caught);
}

TEST(static_init_in_table_order) {
    Setup s;
    store<std::uint32_t>(0x00200000u, 0x00100040u);   // a two-entry ctor table, b before a
    store<std::uint32_t>(0x00200004u, 0x00100030u);
    g_n = 0;
    CallResult r;
    s.nb.run(0x00100050u, args(0x00200000u, 0x00200008u), r);
    CHECK(g_n == 2 && g_order[0] == 2 && g_order[1] == 1);
}

TEST(elf_image_loads_as_data) {
    // a minimal ELF32 LE: one PT_LOAD at 0x100000, 8 file bytes, memsz 16
    std::vector<std::uint8_t> e(0x100 + 8, 0);
    const std::uint8_t ident[] = {0x7F, 'E', 'L', 'F', 1, 1, 1};
    std::memcpy(e.data(), ident, sizeof ident);
    auto w32 = [&](std::size_t o, std::uint32_t v) { std::memcpy(&e[o], &v, 4); };
    w32(0x1C, 0x34);
    e[0x2C] = 1;
    w32(0x34, 1); w32(0x38, 0x100); w32(0x3C, 0x00100000u); w32(0x44, 8); w32(0x48, 16);
    w32(0x100, 0xDEADBEEFu); w32(0x104, 0x01234567u);
    const char* path = "build/test_native_backend.elf";
    std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(e.data()), static_cast<std::streamsize>(e.size()));
    Setup s;
    s.nb.ram()[0x100008] = 0xAA;   // inside memsz past filesz: must be cleared
    std::string err;
    CHECK(s.nb.load_elf(path, err));
    CHECK(load<std::uint32_t>(0x00100000u) == 0xDEADBEEFu);
    CHECK(load<std::uint32_t>(0x80100004u) == 0x01234567u);   // kseg0 mirror
    CHECK(s.nb.ram()[0x100008] == 0);
    CHECK(!s.nb.load_elf("build/does_not_exist.elf", err));
    std::remove(path);
}

TEST(function_info_lookup) {
    Setup s;
    CHECK(function_at(0x00100010u) && std::string(function_at(0x00100010u)->name) == "calls_missing");
    CHECK(function_at(0x00100014u) == nullptr);
    CHECK(function_containing(0x00100014u) && function_containing(0x00100014u)->address == 0x00100010u);
    CHECK(describe_function(0x00100014u).find("inside calls_missing") == 0);
    CHECK(describe_function(0x00300000u) == "unknown");
}

// hostfix: guest RAM and the scratchpad sit inside no-access guards (GuardedRegion). Every byte of both windows is
// usable and starts zeroed, the accessors still work at both ends, and one byte before or past either window faults
// (in a child process: the fault ends it with SIGSEGV / SIGBUS) and is described as a guest memory overrun.
TEST(guest_windows_in_range_and_zeroed) {
    Setup s;
    const auto ram = s.nb.ram();
    const auto spr = s.nb.scratchpad();
    CHECK(ram.size() == kMainRamSize && spr.size() == kScratchpadSize);
    bool zero = true;
    for (std::size_t k = 0; k < ram.size(); k += 4093) zero = zero && ram[k] == 0;
    for (std::size_t k = 0; k < spr.size(); ++k) zero = zero && spr[k] == 0;
    CHECK(zero && ram[ram.size() - 1] == 0);
    store<std::uint32_t>(0x00000000u, 0x11223344u);
    store<std::uint32_t>(kMainRamSize - 4, 0x55667788u);
    store<std::uint32_t>(kScratchpadBase, 0x99AABBCCu);
    store<std::uint32_t>(kScratchpadBase + kScratchpadSize - 4, 0xDDEEFF00u);
    CHECK(load<std::uint32_t>(0x00000000u) == 0x11223344u && load<std::uint32_t>(kMainRamSize - 4) == 0x55667788u);
    CHECK(load<std::uint32_t>(kScratchpadBase) == 0x99AABBCCu);
    CHECK(load<std::uint32_t>(kScratchpadBase + kScratchpadSize - 4) == 0xDDEEFF00u);
    CHECK(host_pointer(kMainRamSize - 4, 4) == ram.data() + kMainRamSize - 4);
    CHECK(host_pointer(kMainRamSize - 4, 5) == nullptr);
    CHECK_THROWS(host_pointer_checked(kMainRamSize - 4, 5));
    CHECK_THROWS(host_pointer_checked(kScratchpadBase + kScratchpadSize - 2, 4));
    s.nb.clear();
    CHECK(load<std::uint32_t>(kMainRamSize - 4) == 0 && load<std::uint32_t>(kScratchpadBase) == 0);
}

TEST(guard_text_names_the_size) {
    Setup s;
    const std::string t = s.nb.guard_text();
    CHECK(t.find("guest RAM guards: ") == 0 && t.find(" MB each") != std::string::npos && t.find("scratchpad 64 KB") != std::string::npos);
    // a direct fallback check: a region whose guards cannot be reserved (more than the address space) is plain memory
    GuardedRegion plain(4096, std::size_t{1} << 62, "test block");
    CHECK(plain.guard() == 0 && plain.data() != nullptr && plain.data()[0] == 0 && plain.data()[4095] == 0);
    plain.data()[4095] = 7;
    char line[200];
    CHECK(!guard_overrun(plain.data() + 4096, line, sizeof line));
}

// the off switch (safe mode asked for, automatic safe mode after a guest memory overrun, B3_GUEST_GUARDS=0; the
// policy: b3settings::safe_mode_guards_off, test_settings safe_mode_guard_policy): the plain blocks, the same windows,
// no guards and none registered
TEST(guards_off_switch) {
    char line[200];
    {
        NativeBackend nb("safe mode after a guest memory overrun");
        CHECK(nb.guard_text() == "guest RAM guards: off (safe mode after a guest memory overrun)" && nb.ram_guard() == 0);
    }
    {
        NativeBackend nb("");   // automatic safe mode, no overrun: on
        CHECK(nb.ram_guard() > 0 && nb.guard_text().find(" MB each") != std::string::npos);
    }
    {
        NativeBackend nb("safe mode");
        BackendScope scope{nb};
        CHECK(nb.guard_text() == "guest RAM guards: off (safe mode)");
        CHECK(nb.ram_guard() == 0 && nb.ram().size() == kMainRamSize && nb.scratchpad().size() == kScratchpadSize);
        CHECK(nb.ram()[0] == 0 && nb.ram()[kMainRamSize - 1] == 0 && nb.scratchpad()[kScratchpadSize - 1] == 0);
        store<std::uint32_t>(kMainRamSize - 4, 0xCAFEF00Du);
        store<std::uint32_t>(kScratchpadBase + kScratchpadSize - 4, 0x0BADF00Du);
        CHECK(load<std::uint32_t>(kMainRamSize - 4) == 0xCAFEF00Du);
        CHECK(load<std::uint32_t>(kScratchpadBase + kScratchpadSize - 4) == 0x0BADF00Du);
        CHECK(!guard_overrun(nb.ram().data() + kMainRamSize, line, sizeof line));
        CHECK(!guard_overrun(nb.scratchpad().data() - 1, line, sizeof line));
    }
#ifndef _WIN32
    setenv("B3_GUEST_GUARDS", "0", 1);
    {
        NativeBackend nb;
        CHECK(nb.guard_text() == "guest RAM guards: off (B3_GUEST_GUARDS=0)" && nb.ram_guard() == 0);
        NativeBackend asked("safe mode");   // an explicit reason wins
        CHECK(asked.guard_text() == "guest RAM guards: off (safe mode)");
    }
    setenv("B3_GUEST_GUARDS", "1", 1);
    {
        NativeBackend nb;   // anything but 0: on
        CHECK(nb.ram_guard() > 0 && nb.guard_text().find(" MB each") != std::string::npos);
    }
    unsetenv("B3_GUEST_GUARDS");
#endif
}

TEST(guard_overrun_is_described) {
    Setup s;
    const auto ram = s.nb.ram();
    const auto spr = s.nb.scratchpad();
    char line[200];
    CHECK(!guard_overrun(ram.data(), line, sizeof line));
    CHECK(!guard_overrun(ram.data() + ram.size() - 1, line, sizeof line));
    CHECK(guard_overrun(ram.data() + ram.size(), line, sizeof line) &&
          std::string(line).find("guest memory overrun") == 0 && std::string(line).find("past the end of guest RAM") != std::string::npos);
    CHECK(guard_overrun(ram.data() - 1, line, sizeof line) && std::string(line).find("before the start of guest RAM") != std::string::npos);
    // the whole 32-bit reach: an in-window pointer plus any 32-bit offset or length stays in the guards (4 GB each
    // side; under an address-space limit, the guard's own far end)
    const std::size_t g = s.nb.ram_guard();
    CHECK(g == (std::size_t{1} << 32) || g >= 0x10000u);
    CHECK(guard_overrun(ram.data() + ram.size() + g - 1, line, sizeof line));
    CHECK(guard_overrun(ram.data() - g, line, sizeof line));
    if (g == (std::size_t{1} << 32)) {
        CHECK(guard_overrun(ram.data() + ram.size() - 1 + 0xFFFFFFFFull, line, sizeof line));
        CHECK(guard_overrun(ram.data() - 0xFFFFFFFFull, line, sizeof line));
    }
    CHECK(guard_overrun(spr.data() + spr.size(), line, sizeof line) && std::string(line).find("the scratchpad") != std::string::npos);
    CHECK(guard_overrun(spr.data() - 1, line, sizeof line));
    int on_stack = 0;
    CHECK(!guard_overrun(&on_stack, line, sizeof line));
}

#ifndef _WIN32
namespace {
// Runs `touch` in a child process; true when the child died of SIGSEGV or SIGBUS (the guard page)
bool faults_in_child(void (*touch)(volatile std::uint8_t*), volatile std::uint8_t* p) {
    std::fflush(stdout);
    const pid_t pid = fork();
    if (pid == 0) {
        signal(SIGSEGV, SIG_DFL);
        signal(SIGBUS, SIG_DFL);
        touch(p);
        _exit(0);   // no fault: the test fails
    }
    int status = 0;
    if (pid < 0 || waitpid(pid, &status, 0) != pid) return false;
    return WIFSIGNALED(status) && (WTERMSIG(status) == SIGSEGV || WTERMSIG(status) == SIGBUS);
}
void read_byte(volatile std::uint8_t* p) { (void)*p; }
void write_byte(volatile std::uint8_t* p) { *p = 1; }
}  // namespace

TEST(one_byte_past_either_window_faults) {
    Setup s;
    std::uint8_t* ram = s.nb.ram().data();
    std::uint8_t* spr = s.nb.scratchpad().data();
    CHECK(faults_in_child(read_byte, ram + kMainRamSize));
    CHECK(faults_in_child(read_byte, ram - 1));
    CHECK(faults_in_child(write_byte, ram + kMainRamSize));
    CHECK(faults_in_child(read_byte, spr + kScratchpadSize));
    CHECK(faults_in_child(read_byte, spr - 1));
    CHECK(faults_in_child(write_byte, spr + kScratchpadSize));
    CHECK(faults_in_child(read_byte, ram + kMainRamSize + s.nb.ram_guard() - 1));   // the guard's far end (4 GB)
    CHECK(faults_in_child(read_byte, ram - s.nb.ram_guard()));
    CHECK(!faults_in_child(read_byte, ram + kMainRamSize - 1));             // the last bytes are fine
    CHECK(!faults_in_child(write_byte, spr));
}

// native_play's crash report (tools/native/crash_report.h) names a fault in a guard: the child's stdout is a pipe
TEST(crash_report_names_the_overrun) {
    Setup s;
    volatile std::uint8_t* past = s.nb.ram().data() + kMainRamSize + 0x1234;
    int fds[2];
    CHECK(pipe(fds) == 0);
    std::fflush(stdout);
    const pid_t pid = fork();
    if (pid == 0) {
        dup2(fds[1], 1);
        close(fds[0]);
        b3native::install_crash_report();
        *past = 1;   // the report ends the process (exit 4)
        _exit(0);
    }
    close(fds[1]);
    std::string out;
    char buf[4096];
    for (ssize_t n; (n = read(fds[0], buf, sizeof buf)) > 0;) out.append(buf, static_cast<std::size_t>(n));
    close(fds[0]);
    int status = 0;
    CHECK(waitpid(pid, &status, 0) == pid && WIFEXITED(status) && WEXITSTATUS(status) == 4);
    CHECK(out.find("crash: guest memory overrun: host address") != std::string::npos &&
          out.find("0x1234 bytes past the end of guest RAM") != std::string::npos);
}
#endif

TEST_MAIN("native backend (S0)")
