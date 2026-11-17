// The native backend (S0, docs/standalone.md §8): arena, ELF image, the strict missing/fault rules, the native call
// chain, static init through the ctor table. Synthetic data only (no disc data).
#include "b3host/call.h"
#include "b3host/native_backend.h"
#include "test.h"

#include <cstdio>
#include <fstream>
#include <vector>

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

TEST_MAIN("native backend (S0)")
