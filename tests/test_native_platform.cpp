// The native backend's platform tier, C runtime part (src/native/platform_crt.cpp), against the retail functions run
// from orig/SLUS_210.50 in tests/r5900_mini.h on random inputs: the return register and every byte of the test area
// (and the destructor chain word) must be equal. Skips without the retail ELF (B3_RETAIL_ELF).
#include "b3host/call.h"
#include "b3host/native_backend.h"
#include "platform.h"
#include "platform_vu0.h"
#include "sha256.h"
#include "vu0_unit.h"
#include "b3asset/vu1.h"
#include "b3hw/hw.h"
#include "r5900_mini.h"
#include "test.h"

#include <atomic>
#include <bit>
#include <memory>
#include <random>

using namespace b3host;

namespace {

constexpr gaddr kArea = 0x01000000u;     // test buffers (retail .bss is far below; RAM above the image is free)
constexpr std::uint32_t kAreaSize = 0x1000u;
constexpr gaddr kSp = 0x01F00000u;

struct Fixture {
    std::unique_ptr<mini::R5900> cpu = std::make_unique<mini::R5900>();
    std::unique_ptr<NativeBackend> nb = std::make_unique<NativeBackend>();
    bool ok = false;
    Fixture() {
#ifdef B3_RETAIL_ELF
        std::string err;
        ok = cpu->load_elf(B3_RETAIL_ELF) && nb->load_elf(B3_RETAIL_ELF, err);
#endif
    }
};
Fixture& fx() { static Fixture f; return f; }

std::mt19937 rng(12345);
std::string g_fail;

// Fills the area identically on both sides, runs retail and native with a0-a3, compares.
template <class Setup>
bool same(gaddr fn, void (*entry)(const CallArgs&, CallResult&), char ret, std::initializer_list<std::uint64_t> args,
          Setup setup) {
    Fixture& f = fx();
    std::vector<std::uint8_t> area(kAreaSize);
    setup(area);
    std::memcpy(f.cpu->ram().data() + kArea, area.data(), kAreaSize);
    std::memcpy(f.nb->ram().data() + kArea, area.data(), kAreaSize);
    const std::uint32_t chain_before = load<std::uint32_t>(0x004E3000u);
    f.cpu->poke32(0x004E3000u, chain_before);
    unsigned i = 0;
    CallArgs a;
    for (std::uint64_t v : args) { f.cpu->set_gpr64(4 + i, v); a.gpr[i] = detail::gpr64(v); ++i; }
    a.n_gpr = a.n_fpr = 8;
    a.sp = kSp;
    f.cpu->set_gpr64(29, kSp);
    f.cpu->run_from(fn);
    CallResult r;
    entry(a, r);
    const std::uint64_t rv = f.cpu->gpr_lo(2), nv = detail::low64(r.v0);
    const bool v_ok = ret == 'n' || (ret == 'i' ? static_cast<std::uint32_t>(rv) == static_cast<std::uint32_t>(nv) : rv == nv);
    const bool m_ok = std::memcmp(f.cpu->ram().data() + kArea, f.nb->ram().data() + kArea, kAreaSize) == 0 &&
                      f.cpu->peek32(0x004E3000u) == load<std::uint32_t>(0x004E3000u);
    if ((!v_ok || !m_ok) && g_fail.empty()) {
        char m[160];
        std::snprintf(m, sizeof m, "%08X: v0 retail %016llx native %016llx, memory %s", fn, (unsigned long long)rv,
                      (unsigned long long)nv, m_ok ? "same" : "differs");
        g_fail = m;
    }
    return v_ok && m_ok;
}

// A random string of length len over a small alphabet (so equal prefixes and case pairs are common).
void put_string(std::vector<std::uint8_t>& area, std::uint32_t at, std::uint32_t len) {
    static const char abc[] = "aAbBzZ09 _\x80\xC1\xE9";
    for (std::uint32_t k = 0; k < len; ++k) area[at + k] = static_cast<std::uint8_t>(abc[rng() % (sizeof abc - 1)]);
    area[at + len] = 0;
}

#define RUN(name, n, ...) do { int bad_ = 0; for (int k_ = 0; k_ < (n); ++k_) if (!(__VA_ARGS__)) ++bad_; \
    if (bad_) std::printf("    %s: %d of %d differ (%s)\n", name, bad_, (n), g_fail.c_str()); CHECK(bad_ == 0); g_fail.clear(); } while (0)

}  // namespace

TEST(c_runtime_against_retail) {
    if (!fx().ok) { std::printf("    (skipped: no retail ELF)\n"); return; }
    BackendScope scope(*fx().nb);
    namespace p = b3native::platform;
    const gaddr A = kArea, B = kArea + 0x400, D = kArea + 0x800;
    auto rnd = [](std::uint32_t m) { return static_cast<std::uint32_t>(rng() % m); };
    auto two = [&](std::uint32_t& la, std::uint32_t& lb) {
        return [&](std::vector<std::uint8_t>& ar) {
            la = rnd(40); lb = rnd(40);
            put_string(ar, 0, la);
            if (rnd(3) == 0) { std::memcpy(&ar[0x400], &ar[0], la + 1); if (la && rnd(2)) ar[0x400 + rnd(la)] ^= 0x20; }
            else put_string(ar, 0x400, lb);
        };
    };
    std::uint32_t la = 0, lb = 0;
    RUN("strlen", 300, same(0x001273E8u, p::strlen_, 'i', {A}, two(la, lb)));
    RUN("strcmp", 600, same(0x00127188u, p::strcmp_, 'i', {A, B}, two(la, lb)));
    RUN("strncmp", 600, same(0x001277A8u, p::strncmp_, 'i', {A, B, rnd(45)}, two(la, lb)));
    RUN("strcasecmp", 600, same(0x00126DF0u, p::strcasecmp_, 'i', {A, B}, two(la, lb)));
    RUN("strncasecmp", 900, same(0x00127520u, p::strncasecmp_, 'i', {A, B, rnd(45)}, two(la, lb)));   // fb1
    RUN("strcpy", 300, same(0x001272D0u, p::strcpy_, 'i', {D + rnd(4), A}, two(la, lb)));
    RUN("strncpy", 300, same(0x00127960u, p::strncpy_, 'i', {D + rnd(4), A, rnd(60)}, two(la, lb)));
    RUN("strcat", 300, same(0x00126EC8u, p::strcat_, 'i', {A, B}, two(la, lb)));
    RUN("strncat", 300, same(0x001275F8u, p::strncat_, 'i', {A, B, rnd(45)}, two(la, lb)));
    RUN("strchr", 300, same(0x00126FF8u, p::strchr_, 'i', {A, std::uint64_t("aAz0\x80"[rnd(5)])}, two(la, lb)));
    RUN("strchr nul", 50, same(0x00126FF8u, p::strchr_, 'i', {A, 0}, two(la, lb)));
    RUN("strrchr", 300, same(0x00127BA8u, p::strrchr_, 'i', {A, std::uint64_t("aAz0\x80"[rnd(5)])}, two(la, lb)));
    RUN("strpbrk", 300, same(0x00127B20u, p::strpbrk_, 'i', {A, B}, [&](std::vector<std::uint8_t>& ar) { put_string(ar, 0, rnd(40)); put_string(ar, 0x400, rnd(4)); }));
    RUN("strstr", 600, same(0x00127BF8u, p::strstr_, 'i', {A, B}, [&](std::vector<std::uint8_t>& ar) {
        const std::uint32_t lh = rnd(40), ln = rnd(4);
        put_string(ar, 0, lh); put_string(ar, 0x400, ln);
        if (lh > ln && rnd(2)) std::memcpy(&ar[0x400], &ar[rnd(lh - ln + 1)], ln); }));
    RUN("func_00129198 (strupr)", 300, same(0x00129198u, p::strupr_, 'i', {A}, [&](std::vector<std::uint8_t>& ar) {
        const std::uint32_t l = rnd(60);
        for (std::uint32_t k = 0; k < l; ++k) ar[k] = static_cast<std::uint8_t>(1 + rnd(255));
        ar[l] = 0; }));
    RUN("memcmp", 600, same(0x00123E10u, p::memcmp_, 'i', {A + rnd(4), B + rnd(4), rnd(64)}, two(la, lb)));
    RUN("memcpy", 400, same(0x00123EA8u, p::memcpy_, 'i', {D + rnd(8), A + rnd(8), rnd(300)}, two(la, lb)));
    RUN("memmove", 400, same(0x00123F58u, p::memmove_, 'i', {A + rnd(64), A + rnd(64), rnd(300)}, two(la, lb)));
    RUN("memset", 400, same(0x00124060u, p::memset_, 'i', {D + rnd(8), rnd(0x300), rnd(600)}, two(la, lb)));
    RUN("__register_global_object", 20, same(0x001007F0u, p::register_global_object, 'l', {A + 0x10, 0x00123456u, D + 4 * rnd(16)}, two(la, lb)));
    auto r64 = [&]() -> std::uint64_t {
        const unsigned s = rnd(4);
        const std::uint64_t v = (std::uint64_t(rng()) << 32) | rng();
        return s == 0 ? v : s == 1 ? (v & 0xFFFFFFFFu) : s == 2 ? std::uint64_t(std::int64_t(std::int32_t(v))) : (v >> rnd(63));
    };
    auto nz = [&]() { std::uint64_t v; do v = r64(); while (v == 0); return v; };
    auto none = [](std::vector<std::uint8_t>&) {};
    RUN("__muldi3", 500, same(0x0012BFA0u, p::muldi3, 'l', {r64(), r64()}, none));
    RUN("__divdi3", 500, same(0x0012C018u, p::divdi3, 'l', {r64(), nz()}, none));
    RUN("__moddi3", 500, same(0x0012C6B0u, p::moddi3, 'l', {r64(), nz()}, none));
    RUN("__udivdi3", 500, same(0x0012CD40u, p::udivdi3, 'l', {r64(), nz()}, none));
    RUN("__umoddi3", 500, same(0x0012D2C0u, p::umoddi3, 'l', {r64(), nz()}, none));
}

// The array constructor calls ctor(element, 1) in order: a hooked fake ctor records the calls on both sides.
namespace {
std::vector<std::pair<std::uint32_t, std::uint32_t>> g_native_calls;
void fake_ctor(const CallArgs& a, CallResult&) { g_native_calls.push_back({a.gpr[0].w[0], a.gpr[1].w[0]}); }
std::atomic<bool> g_on{true};
std::atomic<std::uint64_t> g_nested{0};
}  // namespace

// rt1: qsort (0x125338) with a guest comparator. The comparator is the same on both sides: MIPS code at kCmp for the
// retail run, a native override at kCmp for ours. It logs (a, b) at the area's log pointer (kArea + 0xFF8, while below
// kArea + 0xFF0), so the comparison is also the call order and arguments, then returns (a[0] & 15) - (b[0] & 15): few
// keys, so equal elements (the partition's r == 0 paths, swap_cnt) are common. Element sizes 1..16 (byte swaps and
// 64-bit swaps), bases aligned and not, n up to 50 (the pseudo-median of 9 over 40).
constexpr gaddr kCmp = 0x01800000u;
void native_cmp(const CallArgs& a, CallResult& r) {
    const gaddr x = a.gpr[0].w[0], y = a.gpr[1].w[0];
    const gaddr lp = load<std::uint32_t>(kArea + 0xFF8u);
    if (lp < kArea + 0xFF0u) {
        store<std::uint32_t>(lp, x);
        store<std::uint32_t>(lp + 4, y);
        store<std::uint32_t>(kArea + 0xFF8u, lp + 8);
    }
    const int v = int(load<std::uint8_t>(x) & 15u) - int(load<std::uint8_t>(y) & 15u);
    r.v0 = detail::gpr64(detail::sext32(static_cast<std::uint32_t>(v)));
}

TEST(qsort_against_retail) {
    if (!fx().ok) { std::printf("    (skipped: no retail ELF)\n"); return; }
    BackendScope scope(*fx().nb);
    namespace p = b3native::platform;
    const std::uint32_t code[] = {
        0x3C0A0100u,   // lui   t2, 0x0100
        0x8D4B0FF8u,   // lw    t3, 0xFF8(t2)
        0x254C0FF0u,   // addiu t4, t2, 0xFF0
        0x016C682Bu,   // sltu  t5, t3, t4
        0x11A00005u,   // beq   t5, zero, +5
        0x00000000u,   // nop
        0xAD640000u,   // sw    a0, 0(t3)
        0xAD650004u,   // sw    a1, 4(t3)
        0x256B0008u,   // addiu t3, t3, 8
        0xAD4B0FF8u,   // sw    t3, 0xFF8(t2)
        0x90880000u,   // lbu   t0, 0(a0)
        0x90A90000u,   // lbu   t1, 0(a1)
        0x3108000Fu,   // andi  t0, t0, 15
        0x3129000Fu,   // andi  t1, t1, 15
        0x03E00008u,   // jr    ra
        0x01091023u,   // subu  v0, t0, t1
    };
    for (unsigned k = 0; k < sizeof code / sizeof code[0]; ++k) fx().cpu->poke32(kCmp + 4 * k, code[k]);
    std::atomic<bool> on{true};
    std::atomic<std::uint64_t> nested{0};
    const NativeOverride ov{kCmp, &native_cmp, 16, &on, &nested};
    set_native_overrides(&ov, 1);
    auto rnd = [](std::uint32_t m) { return static_cast<std::uint32_t>(rng() % m); };
    static const std::uint32_t sizes[] = {1, 3, 4, 8, 12, 16};
    for (int pass = 0; pass < 2; ++pass) {   // pass 1: n over 40 only
        RUN(pass ? "qsort n > 40" : "qsort", pass ? 150 : 600, [&] {
            const std::uint32_t es = sizes[rnd(6)];
            const std::uint32_t n = pass ? 41 + rnd(10) : rnd(41);
            const std::uint32_t off = rnd(2) ? 0 : 1 + rnd(7);
            return same(0x00125338u, p::qsort_, 'n', {kArea + off, n, es, kCmp}, [&](std::vector<std::uint8_t>& ar) {
                for (std::uint32_t k = 0; k < 0x400; ++k) ar[k] = static_cast<std::uint8_t>(rng());
                if (rnd(3) == 0)   // sorted / reversed runs sometimes
                    for (std::uint32_t k = 0; k < n; ++k) ar[off + k * es] = static_cast<std::uint8_t>(rnd(2) ? k : n - k);
                const std::uint32_t log = kArea + 0x400u;
                std::memcpy(&ar[0xFF8], &log, 4);
            });
        }());
    }
    set_native_overrides(nullptr, 0);
}

// rt1: strtol / atoi / rand. The reent (errno at +0, rand's state at +0xA8) is moved into the compared area on both
// sides (_impure_ptr 0x4876E8 -> kArea + 0xC00), so errno and the state are compared too. Strings: spaces (the ctype
// space class), a sign, "0x" / "0X", digits and letters of both cases, bytes >= 0x80, up to 70 characters (overflow).
TEST(strtol_atoi_rand_against_retail) {
    if (!fx().ok) { std::printf("    (skipped: no retail ELF)\n"); return; }
    BackendScope scope(*fx().nb);
    namespace p = b3native::platform;
    constexpr gaddr kImpure = 0x004876E8u, kReent = kArea + 0xC00u;
    const std::uint32_t saved = fx().cpu->peek32(kImpure);
    fx().cpu->poke32(kImpure, kReent);
    store<std::uint32_t>(kImpure, kReent);
    auto rnd = [](std::uint32_t m) { return static_cast<std::uint32_t>(rng() % m); };
    auto num = [&](std::vector<std::uint8_t>& ar) {
        static const char pre[][4] = {"", " ", "\t ", "\n", "-", "+", " -", "0x", "0X", "-0x", "0", "x"};
        static const char dig[] = "0123456789abcdefzAFZ9 \x80";
        std::string t = pre[rnd(12)];
        const std::uint32_t n = rnd(4) == 0 ? 20 + rnd(50) : rnd(12);
        for (std::uint32_t k = 0; k < n; ++k) t += rnd(3) ? char('0' + rnd(10)) : dig[rnd(sizeof dig - 1)];
        std::memcpy(&ar[0], t.c_str(), t.size() + 1);
        for (int k = 0; k < 8; ++k) ar[0xC00 + k] = 0;   // errno 0 before
    };
    const gaddr E = kArea + 0x800u;
    static const std::uint32_t bases[] = {0, 2, 8, 10, 16, 36, 7};
    RUN("strtol", 1500, same(0x00128F08u, p::strtol_, 'l', {kArea, rnd(2) ? E : 0, bases[rnd(7)]}, num));
    // the cutoff itself: LONG_MAX / LONG_MIN and one past, in three bases (the `acc == cutoff && c > cutlim` edge)
    static const std::pair<const char*, std::uint32_t> edge[] = {
        {"9223372036854775807", 10}, {"9223372036854775808", 10}, {"-9223372036854775808", 10}, {"-9223372036854775809", 10},
        {"7fffffffffffffff", 16}, {"8000000000000000", 16}, {"-8000000000000000", 16}, {"-8000000000000001", 16},
        {"777777777777777777777", 8}, {"1000000000000000000000", 8}, {"0x7FFFFFFFFFFFFFFF", 0}, {"0x8000000000000000", 0}};
    for (const auto& [str, b] : edge)
        RUN("strtol edge", 1, same(0x00128F08u, p::strtol_, 'l', {kArea, E, b}, [&](std::vector<std::uint8_t>& ar) {
            std::memcpy(&ar[0], str, std::strlen(str) + 1);
            for (int k = 0; k < 8; ++k) ar[0xC00 + k] = 0; }));
    RUN("atoi", 800, same(0x0011FD30u, p::atoi_, 'i', {kArea}, num));
    RUN("rand", 500, same(0x00125D40u, p::rand_, 'i', {}, [&](std::vector<std::uint8_t>& ar) {
        for (int k = 0; k < 8; ++k) ar[0xCA8 + k] = static_cast<std::uint8_t>(rng()); }));
    fx().cpu->poke32(kImpure, saved);
    store<std::uint32_t>(kImpure, saved);
}

TEST(construct_array_against_retail) {
    if (!fx().ok) { std::printf("    (skipped: no retail ELF)\n"); return; }
    BackendScope scope(*fx().nb);
    constexpr gaddr kCtor = 0x01000F00u;   // never executed: hooked on the retail side, a table entry on ours
    const NativeOverride table[] = {{kCtor, &fake_ctor, 1, &g_on, &g_nested}};
    set_native_overrides(table, 1);
    std::vector<std::pair<std::uint32_t, std::uint32_t>> retail_calls;
    fx().cpu->hooks[kCtor] = [&](mini::R5900& c) { retail_calls.push_back({static_cast<std::uint32_t>(c.gpr_lo(4)), static_cast<std::uint32_t>(c.gpr_lo(5))}); };
    int bad = 0;
    for (int k = 0; k < 40; ++k) {
        retail_calls.clear();
        g_native_calls.clear();
        const std::uint32_t size = 4 + 4 * static_cast<std::uint32_t>(rng() % 20), n = static_cast<std::uint32_t>(rng() % 9);
        if (!same(0x00100270u, b3native::platform::construct_array, 'n', {kArea, kCtor, 0x00100004u, size, n},
                  [](std::vector<std::uint8_t>&) {}) || retail_calls != g_native_calls || retail_calls.size() != n)
            ++bad;
    }
    fx().cpu->hooks.erase(kCtor);
    set_native_overrides(nullptr, 0);
    CHECK(bad == 0);
}

// boot1: the printf family (src/native/platform_printf.cpp) against retail snprintf / sprintf / vsprintf on random
// formats: integer, character and string conversions with random flags, width, precision (also *), length; the
// output buffer, the %n targets, the return value and newlib's reent (the first call runs __sinit) must be equal.
namespace {
struct Fmt {
    std::string text;
    std::vector<std::uint64_t> args;
};
Fmt random_format(gaddr strs, gaddr ntarget) {
    auto rnd = [](std::uint32_t m) { return static_cast<std::uint32_t>(rng() % m); };
    auto r64 = [&]() -> std::uint64_t {
        const std::uint64_t v = (std::uint64_t(rng()) << 32) | rng();
        switch (rnd(5)) {
        case 0: return v;
        case 1: return v & 0xFFu;
        case 2: return std::uint64_t(std::int64_t(std::int32_t(v)));
        case 3: return rnd(3);   // 0 / 1 / 2: the zero-value cases
        default: return v & 0xFFFFFFFFu;
        }
    };
    Fmt f;
    const unsigned n = 1 + rnd(5);
    unsigned nn = 0;
    for (unsigned k = 0; k < n; ++k) {
        if (rnd(2)) f.text += "ab "[rnd(3)];
        f.text += '%';
        static const char fl[] = "-+ #0";
        for (unsigned j = rnd(3); j-- > 0;) f.text += fl[rnd(5)];
        if (rnd(4) == 0) { f.text += '*'; f.args.push_back(std::uint64_t(std::int64_t(int(rnd(30)) - 10))); }
        else if (rnd(2)) f.text += std::to_string(rnd(25));
        if (rnd(3) == 0) {
            f.text += '.';
            if (rnd(4) == 0) { f.text += '*'; f.args.push_back(std::uint64_t(std::int64_t(int(rnd(20)) - 5))); }
            else if (rnd(2)) f.text += std::to_string(rnd(12));
        }
        static const char* len[] = {"", "", "", "h", "l", "ll", "q"};
        static const char conv[] = "diouxXcspDOU%n";
        char c = conv[rnd(sizeof conv - 1)];
        if (c == 'n' && nn >= 2) c = 'd';
        if (c != 'c' && c != 's' && c != 'p' && c != '%') f.text += len[rnd(7)];
        f.text += c;
        if (c == 's') f.args.push_back(rnd(6) == 0 ? 0 : strs + 0x40 * rnd(4));
        else if (c == 'n') f.args.push_back(ntarget + 8 * nn++);
        else if (c != '%') f.args.push_back(r64());
    }
    if (rnd(2)) f.text += " z";
    return f;
}
}  // namespace

TEST(printf_family_against_retail) {
    if (!fx().ok) { std::printf("    (skipped: no retail ELF)\n"); return; }
    Fixture& f = fx();
    BackendScope scope(*f.nb);
    namespace p = b3native::platform;
    const gaddr FMT = kArea, STR = kArea + 0x200, OUT = kArea + 0x400, NT = kArea + 0xE00, VA = kArea + 0xC00;
    const gaddr reent = load<std::uint32_t>(0x004876E8u);
    CHECK(reent != 0 && f.cpu->peek32(0x004876E8u) == reent);
    int bad = 0, total = 0;
    std::string first_fail;
    for (int k = 0; k < 1500; ++k) {
        const int which = k % 3;   // snprintf, sprintf, vsprintf
        Fmt fm = random_format(STR, NT);
        std::vector<std::uint8_t> area(kAreaSize, 0xCC);
        std::memcpy(&area[0], fm.text.c_str(), fm.text.size() + 1);
        for (unsigned s = 0; s < 4; ++s) put_string(area, 0x200 + 0x40 * s, static_cast<std::uint32_t>(rng() % 50));
        std::memcpy(f.cpu->ram().data() + kArea, area.data(), kAreaSize);
        std::memcpy(f.nb->ram().data() + kArea, area.data(), kAreaSize);
        std::vector<std::uint64_t> regs;
        const std::uint32_t size = static_cast<std::uint32_t>(rng() % 3 == 0 ? rng() % 8 : 0x300);
        if (which == 0) regs = {OUT, size, FMT};
        else if (which == 1) regs = {OUT, FMT};
        else regs = {OUT, FMT, VA};
        std::vector<std::uint64_t> stack;
        if (which == 2) {
            for (std::size_t j = 0; j < fm.args.size(); ++j) {
                std::memcpy(f.cpu->ram().data() + VA + 8 * j, &fm.args[j], 8);
                std::memcpy(f.nb->ram().data() + VA + 8 * j, &fm.args[j], 8);
            }
        } else {
            for (std::uint64_t v : fm.args) (regs.size() < 8 ? regs : stack).push_back(v);
        }
        CallArgs a;
        for (unsigned i = 0; i < 8; ++i) {
            const std::uint64_t v = i < regs.size() ? regs[i] : 0;
            f.cpu->set_gpr64(4 + i, v);
            a.gpr[i] = detail::gpr64(v);
        }
        a.n_gpr = a.n_fpr = 8;
        a.sp = kSp;
        for (std::size_t j = 0; j < stack.size(); ++j) {
            std::memcpy(f.cpu->ram().data() + kSp + 8 * j, &stack[j], 8);
            std::memcpy(f.nb->ram().data() + kSp + 8 * j, &stack[j], 8);
        }
        f.cpu->set_gpr64(29, kSp);
        const gaddr fn = which == 0 ? 0x00126A18u : which == 1 ? 0x00126B40u : 0x0012BC70u;
        f.cpu->run_from(fn);
        CallResult r;
        (which == 0 ? p::snprintf_ : which == 1 ? p::sprintf_ : p::vsprintf_)(a, r);
        ++total;
        const bool ok = static_cast<std::uint32_t>(f.cpu->gpr_lo(2)) == r.v0.w[0] &&
                        std::memcmp(f.cpu->ram().data() + kArea, f.nb->ram().data() + kArea, kAreaSize) == 0 &&
                        std::memcmp(f.cpu->ram().data() + reent, f.nb->ram().data() + reent, 0x300) == 0;
        if (!ok) {
            ++bad;
            if (first_fail.empty()) {
                gaddr at = 0;
                for (gaddr q = kArea; q < kArea + kAreaSize && !at; ++q) if (f.cpu->ram()[q] != f.nb->ram()[q]) at = q;
                for (gaddr q = reent; q < reent + 0x300 && !at; ++q) if (f.cpu->ram()[q] != f.nb->ram()[q]) at = q;
                std::string rs, ns;
                if (at) for (gaddr q = at; q < at + 12; ++q) { rs += char(f.cpu->ram()[q] >= 32 && f.cpu->ram()[q] < 127 ? f.cpu->ram()[q] : '.'); ns += char(f.nb->ram()[q] >= 32 && f.nb->ram()[q] < 127 ? f.nb->ram()[q] : '.'); }
                char m[400];
                std::snprintf(m, sizeof m, "fn %08X fmt \"%s\" size %u: v0 retail %u native %u; first diff %08X retail \"%s\" native \"%s\"", fn, fm.text.c_str(), size,
                              static_cast<unsigned>(f.cpu->gpr_lo(2)), r.v0.w[0], at, rs.c_str(), ns.c_str());
                first_fail = m;
            }
        }
    }
    if (bad) std::printf("    printf family: %d of %d differ (%s)\n", bad, total, first_fail.c_str());
    CHECK(bad == 0);
    CHECK(load<std::uint32_t>(reent + 0x38) == 1);   // __sinit ran (natively), as in retail
}

// mac4: the floating-point conversions (e E f g G; the race HUD's crash combo text, sprintf "%.1f" in
// GetComboItemDescription 0x364300). Retail's _dtoa_r cannot run in the mini (its Bigints come from the newlib heap:
// sbrk is a syscall the mini does not take), so expected texts: correctly rounded, ties to even on exact binary
// halves, as newlib's dtoa mode 3 [L]; newlib's Inf / NaN spelling
TEST(printf_float_conversions) {
    NativeBackend nb;
    BackendScope scope(nb);
    namespace p = b3native::platform;
    const gaddr FMT = 0x00100000u, OUT = 0x00100400u;
    struct Row { const char* fmt; double v; const char* want; };
    const Row rows[] = {
        {"%.1f", 1.25, "1.2"},          {"%.1f", 1.35, "1.4"},          {"%.1f", 2.0, "2.0"},
        {"x%.1fy", -0.05, "x-0.1y"},    {"%f", 0.5, "0.500000"},        {"%e", 12345.678, "1.234568e+04"},
        {"%g", 0.0001, "0.0001"},       {"%g", 1e-5, "1e-05"},          {"%G", 1e20, "1E+20"},
        {"%10.3f|", 3.14159, "     3.142|"}, {"%-8.2f|", 2.5, "2.50    |"}, {"%08.2f", -1.5, "-0001.50"},
        {"%+.0f", 2.5, "+2"},           {"%#.0f", 3.0, "3."},           {"% .3g", 2.0 / 3.0, " 0.667"},
        {"%f", 1.0 / 0.0, "Inf"},       {"%E", -1.0 / 0.0, "-INF"},     {"%5.1f|", 0.0 / 0.0, "  NaN|"},
    };
    for (const Row& r : rows) {
        for (std::size_t i = 0; i <= std::strlen(r.fmt); ++i) store<std::uint8_t>(FMT + static_cast<gaddr>(i), static_cast<std::uint8_t>(r.fmt[i]));
        CallArgs a;
        const std::uint64_t regs[3] = {OUT, FMT, std::bit_cast<std::uint64_t>(r.v)};
        for (unsigned i = 0; i < 8; ++i) a.gpr[i] = detail::gpr64(i < 3 ? regs[i] : 0);
        a.n_gpr = a.n_fpr = 8;
        a.sp = 0x01F00000u;
        CallResult res;
        p::sprintf_(a, res);
        std::string got;
        for (gaddr q = OUT; load<std::uint8_t>(q); ++q) got.push_back(static_cast<char>(load<std::uint8_t>(q)));
        if (got != r.want) std::printf("    %s of %g: \"%s\", want \"%s\"\n", r.fmt, r.v, got.c_str(), r.want);
        CHECK(got == r.want);
        CHECK(res.v0.w[0] == std::strlen(r.want));
    }
}

// The crt0 stubs: documented behaviour, fatal on what they do not model, logged where guest state differs.
TEST(crt0_stubs) {
    NativeBackend nb;
    BackendScope scope(nb);
    namespace p = b3native::platform;
    CallArgs a;
    CallResult r;
    a.gpr[0] = detail::gpr64(0);
    p::flush_cache(a, r);
    a.gpr[0] = detail::gpr64(2);
    p::flush_cache(a, r);
    a.gpr[0] = detail::gpr64(1);
    CHECK_THROWS(p::flush_cache(a, r));
    // boot2: _InitSys makes supplement_crt0's two semaphores (ids into __sce_sema_id / __sce_eh_sema_id) and, while topId
    // <= 0, InitThread's top thread; topId preset here so no guest thread starts in this test
    nb.set_stack_pointer(0x01FFFD60u);
    store<std::uint32_t>(0x00484158u, 1u);
    const std::size_t semas0 = p::kernel().semas.size();
    p::init_sys(a, r);
    p::init_sys(a, r);
    unsigned initsys_calls = 0;
    for (const auto& e : p::stub_log())
        if (std::string(e.stub) == "_InitSys") initsys_calls = e.calls;
    CHECK(initsys_calls == 2);   // one entry, counted
    CHECK(load<std::uint32_t>(0x00484894u) == load<std::uint32_t>(0x00484890u) + 1u);   // the second call's pair
    CHECK(p::kernel().semas.size() == semas0 + 4u && p::kernel().semas.back().max == 1 && p::kernel().semas.back().count == 1);
    // exit: no atexit handlers -> NativeExit with the code; registered ones -> fatal
    store<std::uint32_t>(0x004876E8u, 0x00500000u);   // _impure_ptr -> a zeroed reent
    a.gpr[0] = detail::gpr64(3);
    bool ended = false;
    try { p::exit_(a, r); } catch (const p::NativeExit& e) { ended = e.code == 3; }
    CHECK(ended);
    store<std::uint32_t>(0x00500000u + 0x148, 0x00500400u);
    bool fatal_ = false;
    try { p::exit_(a, r); } catch (const p::NativeExit&) {} catch (const HostError&) { fatal_ = true; }
    CHECK(fatal_);
}

// boot1: the IOP start-up stubs: the values CB3Game::ConstructHardware's loops need, fatal on unmodelled modes.
TEST(iop_startup_stubs) {
    NativeBackend nb;
    BackendScope scope(nb);
    namespace p = b3native::platform;
    CallArgs a;
    CallResult r;
    auto v0 = [&]() { return static_cast<std::int32_t>(r.v0.w[0]); };
    p::sif_init_rpc(a, r);
    p::sif_reboot_iop(a, r); CHECK(v0() != 0);
    p::sif_sync_iop(a, r); CHECK(v0() != 0);
    // (libcdvd's start-up: the platform rows of platform_devices.cpp, checked per call against retail with B3NATIVE_DIFF)
    a.gpr[0] = detail::gpr64(5);
    CHECK_THROWS(p::sif_init_rpc(a, r));
}

// boot1: newlib's allocator (src/native/platform_malloc.cpp) against retail in the mini: random sequences of malloc /
// free / realloc / calloc / memalign (and failing requests) from the fresh image. After every call: the return value,
// the whole heap from _end (0x1ECEA00) to the higher heap_ptr, __malloc_av_ and the allocator's globals
// (0x4876F8-0x487B58), heap_ptr, the errno words. At the end all RAM below the test stack.
namespace {
struct MallocFixture {
    std::unique_ptr<mini::R5900> cpu = std::make_unique<mini::R5900>();
    std::unique_ptr<NativeBackend> nb = std::make_unique<NativeBackend>();
    bool ok = false;
    MallocFixture() {
#ifdef B3_RETAIL_ELF
        std::string err;
        ok = cpu->load_elf(B3_RETAIL_ELF) && nb->load_elf(B3_RETAIL_ELF, err);
#endif
        cpu->on_syscall = [](mini::R5900& c) {
            const std::int64_t n = static_cast<std::int64_t>(c.gpr_lo(3));
            if (n == 0x2F) c.set_gpr64(2, 1);                       // GetThreadId: the main thread
            else if (n == 0x3E) c.set_gpr64(2, 0x01FFFD60u);        // EndOfHeap (ee-kernel.md §9)
            else if (n == 0x44 || n == 0x42) c.set_gpr64(2, c.gpr_lo(4));   // WaitSema / SignalSema
            else throw std::runtime_error("malloc test: unexpected syscall " + std::to_string(n));
        };
    }
};
}  // namespace

TEST(malloc_family_against_retail) {
    MallocFixture f;
    if (!f.ok) { std::printf("    (skipped: no retail ELF)\n"); return; }
    BackendScope scope(*f.nb);
    namespace p = b3native::platform;
    constexpr gaddr kStack = 0x01FFF000u, kHeap = 0x01ECEA00u, kHeapPtr = 0x00484154u;
    std::mt19937 r(777);
    std::vector<std::uint32_t> live;
    int bad = 0, calls = 0;
    std::string first;
    auto run = [&](gaddr fn, void (*entry)(const CallArgs&, CallResult&), std::initializer_list<std::uint32_t> args,
                   const char* what) -> std::uint32_t {
        unsigned i = 0;
        CallArgs a;
        for (std::uint32_t v : args) { f.cpu->set_gpr64(4 + i, v); a.gpr[i] = detail::gpr64(v); ++i; }
        a.n_gpr = a.n_fpr = 8;
        a.sp = kStack;
        f.cpu->set_gpr64(29, kStack);
        f.cpu->max_steps = 20000000;
        f.cpu->run_from(fn);
        CallResult res;
        entry(a, res);
        ++calls;
        const std::uint32_t rv = static_cast<std::uint32_t>(f.cpu->gpr_lo(2)), nv = res.v0.w[0];
        const std::uint32_t hp = std::max(f.cpu->peek32(kHeapPtr), load<std::uint32_t>(kHeapPtr));
        const std::uint8_t* A = f.cpu->ram().data();
        const std::uint8_t* B = f.nb->ram().data();
        std::string why;
        if (rv != nv) why = "v0";
        else if (std::memcmp(A + 0x004876E0u, B + 0x004876E0u, 0x487B58u - 0x4876E0u) != 0) why = "globals";
        else if (std::memcmp(A + kHeapPtr, B + kHeapPtr, 4) != 0 || std::memcmp(A + 0x01ECE340u, B + 0x01ECE340u, 4) != 0) why = "heap_ptr / errno";
        else if (hp > kHeap && std::memcmp(A + kHeap, B + kHeap, std::min<std::uint32_t>(hp, kStack - 0x1000) - kHeap) != 0) why = "heap";
        if (!why.empty()) {
            ++bad;
            if (first.empty()) {
                char m[200];
                std::snprintf(m, sizeof m, "call %d %s: %s differs (v0 retail %08X native %08X)", calls, what, why.c_str(), rv, nv);
                first = m;
            }
        }
        return rv;
    };
    auto fill = [&](std::uint32_t m, std::uint32_t n) {   // the same pattern on both sides, so copies are visible
        for (std::uint32_t k = 0; k < n; ++k) {
            const std::uint8_t v = static_cast<std::uint8_t>(m + k * 7);
            f.cpu->ram()[m + k] = v;
            f.nb->ram()[m + k] = v;
        }
    };
    auto size_of = [&]() -> std::uint32_t {
        switch (r() % 8) {
        case 0: return r() % 16;
        case 1: case 2: return r() % 200;
        case 3: case 4: return 200 + r() % 1000;
        case 5: return 1000 + r() % 8000;
        case 6: return 8000 + r() % 40000;
        default: return r() % 600;
        }
    };
    std::uint64_t total = 0;
    for (int step = 0; step < 3000 && bad == 0; ++step) {
        const unsigned op = r() % 12;
        if (op < 4 || live.empty()) {
            const std::uint32_t n = size_of();
            if (total + n > 700000) { --step; goto free_one; }
            const std::uint32_t m = run(0x001232D8u, p::malloc_, {n}, "malloc");
            if (m) { live.push_back(m); fill(m, n); total += n; }
            continue;
        }
        if (op == 4) {
            const std::uint32_t n = 1 + r() % 40, s = 1 + r() % 300;
            if (total + n * s > 700000) continue;
            const std::uint32_t m = run(0x0011FD98u, p::calloc_, {n, s}, "calloc");
            if (m) { live.push_back(m); total += n * s; }
            continue;
        }
        if (op == 5) {
            const std::uint32_t al = 16u << (r() % 6), n = size_of();
            if (total + n + al > 700000) continue;
            const std::uint32_t m = run(0x001230A8u, p::memalign_, {al, n}, "memalign");
            if (m) { live.push_back(m); fill(m, n); total += n; }
            continue;
        }
        if (op == 6) {
            const std::size_t k = r() % live.size();
            const std::uint32_t n = size_of();
            if (total + n > 700000) continue;
            const std::uint32_t m = run(0x00125E08u, p::realloc_, {live[k], n}, "realloc");
            if (m) { live[k] = m; total += n; }
            continue;
        }
        if (op == 7 && r() % 20 == 0) {   // a request the heap cannot hold: sbrk's ENOMEM path
            run(0x001232D8u, p::malloc_, {0x00200000u}, "malloc (too big)");
            continue;
        }
        if (op == 8 && r() % 15 == 0) {   // free everything: coalescing into top and malloc_trim
            for (std::uint32_t m : live) run(0x00123330u, p::free_, {m}, "free (all)");
            live.clear();
            total = 0;
            continue;
        }
    free_one:
        if (!live.empty()) {
            const std::size_t k = r() % live.size();
            run(0x00123330u, p::free_, {live[k]}, "free");
            live.erase(live.begin() + static_cast<std::ptrdiff_t>(k));
        }
    }
    run(0x00123330u, p::free_, {0}, "free(0)");
    // targeted realloc paths from a fresh image (the random sequence rarely reaches them)
    auto reset = [&]() {
#ifdef B3_RETAIL_ELF
        std::string err;
        f.cpu->load_elf(B3_RETAIL_ELF);
        f.nb->load_elf(B3_RETAIL_ELF, err);
#endif
    };
    auto top_size = [&]() { const std::uint32_t t = f.cpu->peek32(0x004876F8u + 8); return f.cpu->peek32(t + 4) & ~3u; };
    // (1) growing the chunk next to a too-small top: malloc extends top contiguously, the new chunk is the old one's
    //     neighbour, no copy (realloc's "newp == next_chunk(oldp)")
    reset();
    {
        const std::uint32_t x = run(0x001232D8u, p::malloc_, {100}, "s1 malloc");
        const std::uint32_t y = run(0x001232D8u, p::malloc_, {100}, "s1 malloc");
        fill(x, 100); fill(y, 100);
        run(0x00125E08u, p::realloc_, {y, 300000}, "s1 realloc grows past top");
    }
    // (2) backward into top: the free chunk before and top together hold the request, top alone does not
    for (std::uint32_t extra = 16; extra <= 1024; extra += 112) {
        reset();
        const std::uint32_t a = run(0x001232D8u, p::malloc_, {1000}, "s2 malloc a");
        const std::uint32_t b = run(0x001232D8u, p::malloc_, {1000}, "s2 malloc b");
        fill(a, 1000); fill(b, 1000);
        run(0x00123330u, p::free_, {a}, "s2 free a");
        const std::uint32_t want = 1000 + top_size() + extra;   // > b + top, < a + b + top
        run(0x00125E08u, p::realloc_, {b, want}, "s2 realloc backward into top");
    }
    // (3) backward into a free next chunk and the free previous one
    reset();
    {
        const std::uint32_t a = run(0x001232D8u, p::malloc_, {400}, "s3 malloc a");
        const std::uint32_t b = run(0x001232D8u, p::malloc_, {400}, "s3 malloc b");
        const std::uint32_t c = run(0x001232D8u, p::malloc_, {400}, "s3 malloc c");
        run(0x001232D8u, p::malloc_, {400}, "s3 malloc d (keeps c off top)");
        fill(a, 400); fill(b, 400); fill(c, 400);
        run(0x00123330u, p::free_, {a}, "s3 free a");
        run(0x00123330u, p::free_, {c}, "s3 free c");
        run(0x00125E08u, p::realloc_, {b, 1100}, "s3 realloc into prev + next");
    }
    const bool all_ram = std::memcmp(f.cpu->ram().data(), f.nb->ram().data(), kStack - 0x1000) == 0;
    std::printf("    %d calls; heap_ptr %08X; %s\n", calls, load<std::uint32_t>(kHeapPtr), all_ram ? "all RAM equal" : "RAM differs");
    if (bad) std::printf("    malloc family: %d calls differ (%s)\n", bad, first.c_str());
    CHECK(bad == 0);
    CHECK(all_ram);
    CHECK(calls > 2000);
}

// boot1: the frame scheduler's sleep / VBlank / wakeup path: a VBlank handler (here a fake native one, as the generated
// func_002370A0 is) that calls iWakeupThread ends SleepThread; without one, SleepThread is a reported deadlock.
namespace {
int g_vblank_calls = 0;
void fake_vblank_handler(const CallArgs& a, CallResult& r) {
    ++g_vblank_calls;
    CallArgs w;
    w.gpr[0] = detail::gpr64(1);
    CallResult wr;
    if (a.gpr[0].w[0] == 2) b3native::platform::wakeup_thread(w, wr);   // cause 2 = VBlank start
    r = CallResult{};
}
std::atomic<bool> g_vb_on{true};
std::atomic<std::uint64_t> g_vb_nested{0};
}  // namespace
TEST(vblank_sleep_wakeup) {
    NativeBackend nb;
    BackendScope scope(nb);
    namespace p = b3native::platform;
    constexpr gaddr kHandler = 0x00100004u;
    NativeOverride table[] = {{kHandler, &fake_vblank_handler, 0, &g_vb_on, &g_vb_nested}};
    set_native_overrides(table, 1);
    nb.set_stack_pointer(0x01F00000u);
    CallArgs a;
    CallResult r;
    a.gpr[0] = detail::gpr64(2);
    a.gpr[1] = detail::gpr64(kHandler);
    p::add_intc_handler(a, r);
    p::enable_intc(a, r);
    const std::uint64_t before = p::vblank_count();
    p::sleep_thread(a, r);
    CHECK(g_vblank_calls == 1 && p::vblank_count() == before + 1);
    CallArgs main_thread;
    main_thread.gpr[0] = detail::gpr64(1);
    p::wakeup_thread(main_thread, r);   // a wakeup before the sleep is consumed without a VBlank
    p::sleep_thread(a, r);
    CHECK(g_vblank_calls == 1);
    p::disable_intc(a, r);           // VBlank start off: nothing wakes the thread
    CHECK_THROWS(p::sleep_thread(a, r));
    set_native_overrides(nullptr, 0);
}

// boot2: the hardware model's write rules retail relies on (src/native/hw_model.h)
TEST(hw_model_rules) {
    b3native::HwModel m;
    auto w32 = [&](std::uint32_t a, std::uint32_t v) { std::uint8_t d[8] = {}; std::memcpy(d, &v, 4); m.write(a, 4, d); };
    auto r32 = [&](std::uint32_t a) { std::uint8_t d[8] = {}; m.read(a, 4, d); std::uint32_t v; std::memcpy(&v, d, 4); return v; };
    CHECK(m.gs_csr() == 0x551B0000u && m.gs_imr() == 0x7F00u);   // ee-kernel.md §8
    // D_STAT: status bits write 1 to clear, mask bits write 1 to toggle
    w32(0x1000A020u, 13);           // D2_QWC
    w32(0x1000A000u, 0x101);        // D2_CHCR STR: the transfer completes at once
    CHECK((r32(0x1000A000u) & 0x100u) == 0 && r32(0x1000A020u) == 0 && (m.d_stat() & 4u) != 0);
    w32(0x1000E010u, 4);
    CHECK((m.d_stat() & 4u) == 0);
    w32(0x1000E010u, 0x00040000u);
    CHECK((m.d_stat() >> 16) == 4u);
    w32(0x1000E010u, 0x00040000u);
    CHECK((m.d_stat() >> 16) == 0u);
    // a pending DMAC interrupt is offered to the owner; refused (interrupts off) it stays pending
    unsigned offered = 0;
    bool accept = false;
    m.on_dmac_interrupt = [&](unsigned c) { ++offered; if (accept) m.ack_dmac(c); return accept; };
    m.set_dmac_mask(1u << 2);
    w32(0x1000A000u, 0x101);
    CHECK(offered == 1 && (m.d_stat() & 4u) != 0);
    accept = true;
    m.poll_interrupts();
    CHECK(offered == 2 && (m.d_stat() & 4u) == 0);
    // INTC_STAT: a VBlank poll asks for the next VBlank; write 1 to clear
    unsigned vblanks = 0;
    m.on_vblank = [&] { ++vblanks; m.vblank_start(); };
    CHECK((r32(0x1000F000u) & 4u) != 0 && vblanks == 1);
    CHECK((r32(0x1000F000u) & 4u) != 0 && vblanks == 1);   // already set: no new VBlank
    w32(0x1000F000u, 4);
    CHECK((r32(0x1000F000u) & 4u) != 0 && vblanks == 2);
    // GS CSR RESET restores the reset values
    m.set_gs_imr(0xFF00u);
    std::uint8_t d[8] = {0, 2, 0, 0, 0, 0, 0, 0};
    m.write(0x12001000u, 8, d);
    CHECK(m.gs_imr() == 0x7F00u);
    CHECK_THROWS(r32(0x10004000u));
    // the IPU is not in the native platform (the movies are native): its registers, FIFOs and a transfer on its DMA
    // channels are fatal; the channels' registers stay plain (sceDmaReset clears them)
    CHECK_THROWS(r32(0x10002010u));
    CHECK_THROWS(w32(0x10002000u, 0));
    w32(0x1000B080u, 0);
    CHECK(r32(0x1000B000u) == 0);
    CHECK_THROWS(w32(0x1000B400u, 0x101));
}

// boot1: the b3hw hooks: the native backend hands hardware accesses to the platform model (fatal on unmodelled
// registers), the trace records them in order; the mini's on_mmio sees retail's accesses (the proof's other side).
TEST(b3hw_hooks) {
    NativeBackend nb;
    BackendScope scope(nb);
    std::vector<HwAccess> trace;
    set_hw_trace(&trace);
    CHECK_THROWS(b3hw_write32(0x12001000u, 0x200));   // no model installed: NativeFault
    b3native::platform::install_hw_model(nb);
    CHECK_THROWS(b3hw_write64(0x12001080u, 0x200));   // the model: SIGLBLID not modelled, fatal with the register's name
    CHECK_THROWS(b3hw_read32(0x10004000u));             // VIF0 FIFO: not modelled
    set_hw_trace(nullptr);
    CHECK(trace.size() == 2 && trace[0].address == 0x12001000u && trace[0].write && trace[1].width == 8);
    auto cpu_p = std::make_unique<mini::R5900>();
    mini::R5900& cpu = *cpu_p;
    cpu.ram().assign(mini::R5900::kRamSize, 0);   // (RAM is allocated by load_elf otherwise)
    std::vector<std::pair<std::uint32_t, unsigned>> seen;
    cpu.on_mmio = [&](std::uint32_t phys, unsigned width, bool write, std::uint8_t*) { if (write) seen.push_back({phys, width}); };
    // sw zero, 0(a0) with a0 = 0xB2001000 (kseg1 GS CSR): the store reaches on_mmio at the physical address
    cpu.poke32(0x00100000u, 0xAC800000u);
    cpu.poke32(0x00100004u, 0x03E00008u);   // jr ra
    cpu.poke32(0x00100008u, 0x00000000u);
    cpu.set_gpr64(4, 0xFFFFFFFFB2001000ull);
    cpu.set_gpr64(29, 0x01F00000u);
    cpu.run_from(0x00100000u);
    CHECK(seen.size() == 1 && seen[0].first == 0x12001000u && seen[0].second == 4);
}

// vu0: VIF0 chain DMA (CHCR 0x145 as CGtVu0CodeManager starts it at 0x273508; H4: the dev build's walker) loads VU0 micro
// memory through MPG; vu0_micro_call runs a launch on a b3asm::State; the mini's vcallms / vcallmsr reach it.
// vu0n: the SHA-256 Vu0Unit matches the micro memory with (FIPS 180-4 examples; one and two final blocks)
TEST(vu0_sha256) {
    CHECK(b3native::sha256_hex(b3native::sha256("", 0)) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(b3native::sha256_hex(b3native::sha256("abc", 3)) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    const char* m = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    CHECK(b3native::sha256_hex(b3native::sha256(m, 56)) == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

// vu0n: a launch no generated program covers -- counted and interpreted in an interpreter build (this test's), fatal
// under strict (native_play --strict) with the image's SHA-256 in the message (the default build: always fatal)
TEST(vu0_uncovered_launch) {
    b3native::Vu0Unit u(b3native::Vu0Unit::Timing::kPair);
    const std::uint32_t pairs[2][2] = {{0x1001002Au, 0x400002FFu}, {0x8000033Cu, 0x000002FFu}};   // iaddiu vi1, vi0, 42 [E]; delay
    std::memcpy(u.micro_bytes(), pairs, sizeof pairs);
    b3asm::State s;
    u.call(s, 0, 0x1234);
    CHECK(s.vi[1] == 42 && u.calls() == 1 && u.uncovered() == 1 && u.interpreted() == 1 && u.aot_launches() == 0);
    u.set_strict(true);
    std::string msg;
    try { u.call(s, 0, 0x1234); } catch (const b3host::HostError& e) { msg = e.what(); }
    CHECK(msg.find("no generated program") != std::string::npos);
    CHECK(msg.find(b3native::sha256_hex(b3native::sha256(u.micro_bytes(), 4096))) != std::string::npos);
    CHECK(msg.find("pc 00001234") != std::string::npos);
}

TEST(vu0_vif0_upload_and_vcallms) {
    std::vector<std::uint8_t> ram(0x2000000u, 0);
    auto put = [&](std::uint32_t a, std::uint32_t v) { std::memcpy(&ram[a], &v, 4); };
    b3native::HwModel m;
    m.read_ram = [&](std::uint32_t a, void* out, unsigned n) {
        if (a + n > ram.size()) return false;
        std::memcpy(out, &ram[a], n);
        return true;
    };
    // H4: the platform's VIF0 upload (this test's interpreter build walks the chain into micro memory and checks the
    // address lookup against the SHA-256 path: a hand-made chain is none of retail's images on both sides)
    std::function<std::uint32_t(std::uint32_t)> rd = [&](std::uint32_t a) { std::uint32_t v = 0; std::memcpy(&v, &ram[a], 4); return v; };
    m.on_vif0_kick = [&](std::uint32_t chcr, std::uint32_t tadr) { b3native::platform::vif0_upload(chcr, tadr, rd); };
    auto w32 = [&](std::uint32_t a, std::uint32_t v) { std::uint8_t d[8] = {}; std::memcpy(d, &v, 4); m.write(a, 4, d); };
    // cnt tag (qwc 2) whose TTE half is NOP, MPG 3 pairs at pair 10; then the 3 pairs and two NOPs; an end tag
    const std::uint32_t pairs[3][2] = {
        {0x8000033Cu, 0x01E208E8u},                // add.xyzw vf3, vf1, vf2
        {0x1001002Au, 0x400002FFu},                // iaddiu vi1, vi0, 42  [E]
        {0x8000033Cu, 0x000002FFu},                // delay slot
    };
    put(0x1000, 0x10000002u); put(0x1004, 0); put(0x1008, 0); put(0x100C, 0x4A03000Au);
    for (int k = 0; k < 3; ++k) { put(0x1010 + 8 * k, pairs[k][0]); put(0x1014 + 8 * k, pairs[k][1]); }
    put(0x1030, 0x70000000u);
    w32(0x10008020u, 0);
    w32(0x10008030u, 0x1000);
    w32(0x10008000u, 0x145);
    CHECK((m.d_stat() & 1u) != 0);
    std::uint32_t lo = 0, up = 0;
    std::memcpy(&lo, b3native::platform::vu0_micro_bytes() + 8 * 11, 4);
    std::memcpy(&up, b3native::platform::vu0_micro_bytes() + 8 * 11 + 4, 4);
    CHECK_BITS(lo, 0x1001002Au);
    CHECK_BITS(up, 0x400002FFu);
    b3asm::State s;
    s.vf[1] = b3asm::make_u128(t::fbits(1), t::fbits(2), t::fbits(3), t::fbits(4));
    s.vf[2] = b3asm::make_u128(t::fbits(10), t::fbits(20), t::fbits(30), t::fbits(40));
    const std::uint64_t calls = b3native::platform::vu0_micro_calls();
    b3native::platform::vu0_micro_call(s, 10);
    CHECK(t::bitsf(s.vf[3].w[0]) == 11 && t::bitsf(s.vf[3].w[3]) == 44);
    CHECK(s.vi[1] == 42);
    // the mini: vcallms 0x50 (pair 10), then ctc2 $8, $vi27 (CMSAR0 = 10) and vcallmsr
    mini::R5900 c;
    c.ram().assign(0x2000000u, 0);
    c.on_vcallms = [](mini::R5900& x, std::uint32_t e) { b3native::platform::vu0_micro_call(x.vu, e, x.pc()); };
    const std::uint32_t code[] = {0x4A000038u | (10u << 6), 0x48C8D800u, 0x4A000039u, 0x03E00008u, 0};
    for (unsigned k = 0; k < 5; ++k) c.poke32(0x2000 + 4 * k, code[k]);
    c.vu.vf[1] = s.vf[1];
    c.vu.vf[2] = b3asm::make_u128(t::fbits(0.5f), 0, 0, 0);
    c.set_gpr64(8, 10);
    c.run_from(0x2000);
    CHECK(t::bitsf(c.vu.vf[3].w[0]) == 1.5f && c.vu.vi[1] == 42 && c.cmsar0 == 10);
    CHECK(b3native::platform::vu0_micro_calls() == calls + 3);
#if defined(B3_RETAIL_ELF) && defined(B3_VU_DIR)
    // the real upload: retail's DMA chain vu_game_004787D0 in the ELF image through the walker equals the decomp's
    // image of it parsed by VuMicroprogram::load_textbin (an independent reader)
    Fixture& f = fx();
    if (f.ok) {
        rd = [&](std::uint32_t a) { std::uint32_t v = 0; std::memcpy(&v, f.cpu->ram().data() + a, 4); return v; };
        w32(0x10008030u, 0x004787D0u);
        w32(0x10008000u, 0x145);
        std::vector<std::uint8_t> img;
        if (std::FILE* fp = std::fopen(B3_VU_DIR "/game_004787D0.textbin.bin", "rb")) {
            std::uint8_t b[4096];
            std::size_t n;
            while ((n = std::fread(b, 1, sizeof b, fp)) > 0) img.insert(img.end(), b, b + n);
            std::fclose(fp);
        }
        b3asset::VuMicroprogram p;
        CHECK(p.load_textbin(img));
        CHECK(p.loaded == 452);
        unsigned differ = 0;
        for (unsigned k = 0; k < 452; ++k) {
            std::uint32_t l = 0, u = 0;
            std::memcpy(&l, b3native::platform::vu0_micro_bytes() + 8 * k, 4);
            std::memcpy(&u, b3native::platform::vu0_micro_bytes() + 8 * k + 4, 4);
            differ += l != p.lower[k] || u != p.upper[k];
        }
        CHECK(differ == 0);
    }
#endif
}


// vu0 (boot2-agreed): with a clock, a DMAC callback is delivered kDmacCompletionDelay (32, ee-kernel.md §8) cycles
// after the kick, in kick order (FIFO, a later kick never overtakes), while the D_STAT status bit is set at once
TEST(hw_model_dmac_completion_delay) {
    b3native::HwModel m;
    std::uint64_t now = 1000;
    std::vector<unsigned> got;
    bool allow = true;
    m.clock = [&] { return now; };
    std::vector<std::uint8_t> ram(0x10000, 0);   // VIF1 normal mode, QWC 0: nothing to walk
    m.read_ram = [&](std::uint32_t a, void* out, unsigned n) { if (a + n > ram.size()) return false; std::memcpy(out, &ram[a], n); return true; };
    m.on_dmac_interrupt = [&](unsigned c) { if (!allow) return false; got.push_back(c); m.ack_dmac(c); return true; };
    auto w32 = [&](std::uint32_t a, std::uint32_t v) { std::uint8_t d[8] = {}; std::memcpy(d, &v, 4); m.write(a, 4, d); };
    m.set_dmac_mask((1u << 1) | (1u << 2));
    w32(0x1000A000u, 0x101);                         // GIF (ch2) at t = 1000
    CHECK((m.d_stat() & 4u) != 0 && got.empty());     // status at once, callback not yet
    CHECK(m.next_completion_ready() == 1032);
    now = 1031; m.poll_interrupts();
    CHECK(got.empty());
    now = 1010 + 21;                                  // still 1031
    w32(0x10009000u, 0x101);                         // VIF1 (ch1) kicked later, at t = 1031: ready 1063
    now = 1032; m.poll_interrupts();
    CHECK(got.size() == 1 && got[0] == 2);
    now = 1062; m.poll_interrupts();
    CHECK(got.size() == 1);
    // refused (interrupts off): stays pending, delivered at the next allowed check
    allow = false; now = 1063; m.poll_interrupts();
    CHECK(got.size() == 1 && m.queued_completions() == 0 && m.pending_dmac().size() == 1);
    allow = true; m.poll_interrupts();
    CHECK(got.size() == 2 && got[1] == 1 && m.pending_dmac().empty());
    // FIFO across channels: ch2 kicked first, then ch1 at the same time: delivered 2 then 1
    got.clear(); now = 2000;
    w32(0x1000A000u, 0x101);
    w32(0x10009000u, 0x101);
    now = 2032; m.poll_interrupts();
    CHECK(got.size() == 2 && got[0] == 2 && got[1] == 1);
    // a channel that is not enabled stays pending (the pending list), delivered once enabled
    got.clear(); m.set_dmac_mask(1u << 2);
    w32(0x10009000u, 0x101);
    now = 2100; m.poll_interrupts();
    CHECK(got.empty() && m.pending_dmac().size() == 1);
    m.set_dmac_mask((1u << 1) | (1u << 2));
    CHECK(got.size() == 1 && got[0] == 1);
}

// vu0: scratchpad DMA moves data (ee-kernel.md §8):
// toSPR / fromSPR normal mode copy QWC qwords at the kick, MADR / SADR advance, QWC 0, STR clear; their callbacks are
// queued with delay 0 (the other channels' 32)
TEST(hw_model_spr_dma) {
    b3native::HwModel m;
    std::vector<std::uint8_t> ram(0x10000, 0), spr(0x4000, 0);
    for (unsigned k = 0; k < 64; ++k) ram[0x1000 + k] = static_cast<std::uint8_t>(k + 1);
    m.read_ram = [&](std::uint32_t a, void* out, unsigned n) { if (a + n > ram.size()) return false; std::memcpy(out, &ram[a], n); return true; };
    m.write_ram = [&](std::uint32_t a, const void* in, unsigned n) { if (a + n > ram.size()) return false; std::memcpy(&ram[a], in, n); return true; };
    m.scratchpad = [&] { return spr.data(); };
    std::uint64_t now = 500;
    std::vector<unsigned> got;
    m.clock = [&] { return now; };
    m.on_dmac_interrupt = [&](unsigned c) { got.push_back(c); m.ack_dmac(c); return true; };
    m.has_dmac_handler = [](unsigned) { return true; };
    auto w32 = [&](std::uint32_t a, std::uint32_t v) { std::uint8_t d[8] = {}; std::memcpy(d, &v, 4); m.write(a, 4, d); };
    m.set_dmac_mask((1u << 8) | (1u << 9));
    w32(0x1000D410u, 0x1000);       // toSPR MADR
    w32(0x1000D420u, 4);            // QWC
    w32(0x1000D480u, 0x2000);       // SADR
    w32(0x1000D400u, 0x100);        // STR, normal mode
    CHECK(std::memcmp(&spr[0x2000], &ram[0x1000], 64) == 0);
    CHECK(m.chan(9, 1) == 0x1040 && m.chan(9, 2) == 0 && m.chan(9, 8) == 0x2040 && (m.chan(9, 0) & 0x100u) == 0);
    CHECK(got.size() == 1 && got[0] == 9);   // delay 0: delivered in the same check
    w32(0x1000D010u, 0x3000);       // fromSPR MADR
    w32(0x1000D020u, 2);
    w32(0x1000D080u, 0x2010);       // SADR: qwords 1 and 2 of the copy
    w32(0x1000D000u, 0x100);
    CHECK(std::memcmp(&ram[0x3000], &ram[0x1010], 32) == 0);
    CHECK(m.chan(8, 1) == 0x3020 && m.chan(8, 8) == 0x2030);
    CHECK(got.size() == 2 && got[1] == 8);
    CHECK_THROWS(w32(0x1000D400u, 0x108));   // toSPR interleave mode: not modelled
    CHECK_THROWS(w32(0x1000D400u, 0x104));   // toSPR chain mode: only retail libmpeg used it (dev: hw_model_dev.h)
    CHECK_THROWS(w32(0x1000D000u, 0x104));   // fromSPR chain mode: not modelled
}

// H3 (the device-state work): PATH3 masking without a VIF code walk -- RW's 0x21 gate as rwDMAHandler drives it: the
// FIFO's MSKPATH3(1) sets GIF_STAT.M3P; the GIF chain kicked under it completes at once and latches GIF_STAT.FQC;
// the VIF1 chain clears both; the GIF completion is queued before VIF1's at the same clock (FIFO order)
TEST(hw_model_path3_gate) {
    b3native::HwModel m;
    std::uint64_t now = 7000;
    std::vector<unsigned> got;
    m.clock = [&] { return now; };
    m.on_dmac_interrupt = [&](unsigned c) { got.push_back(c); m.ack_dmac(c); return true; };
    m.has_dmac_handler = [](unsigned) { return true; };
    auto w32 = [&](std::uint32_t a, std::uint32_t v) { std::uint8_t d[8] = {}; std::memcpy(d, &v, 4); m.write(a, 4, d); };
    auto r32 = [&](std::uint32_t a) { std::uint8_t d[8] = {}; m.read(a, 4, d); std::uint32_t v; std::memcpy(&v, d, 4); return v; };
    m.set_dmac_mask((1u << 1) | (1u << 2));
    CHECK((r32(0x10003020u) & 2u) == 0);
    std::uint8_t q[16] = {0x00, 0x80, 0x00, 0x06};                  // {MSKPATH3(1), 0, 0, 0}
    m.write(0x10005000u, 16, q);
    CHECK((r32(0x10003020u) & 2u) != 0 && (r32(0x10003020u) & 0x1F000000u) == 0);
    w32(0x1000A000u, 0x104);                                         // GIF chain: completes, FQC latched
    CHECK((r32(0x1000A000u) & 0x100u) == 0 && (m.d_stat() & 4u) != 0);
    CHECK((r32(0x10003020u) & 0x1F000000u) != 0 && m.fqc_latched());
    w32(0x10009000u, 0x145);                                         // VIF1 chain: unmasks
    CHECK((r32(0x10003020u) & 0x1F000002u) == 0 && !m.mskpath3());
    CHECK(m.queued_completions() == 2 && m.next_completion_ready() == 7032);
    now = 7032; m.poll_interrupts();
    CHECK(got.size() == 2 && got[0] == 2 && got[1] == 1);
    // sceGsResetPath's init qword: MSKPATH3(0) not first in it
    m.write(0x10005000u, 16, q);
    const std::uint8_t init[16] = {0, 0, 0, 0x06, 0, 0, 0, 0x03, 0, 0, 0, 0x02, 0, 0, 0, 0x04};
    m.write(0x10005000u, 16, init);
    CHECK(!m.mskpath3());
    // a GIF kick with PATH3 unmasked: no latch
    w32(0x1000A000u, 0x104);
    CHECK(!m.fqc_latched());
}

// vu0: COP0 condition 0 for the decomp's bc0f DMA waits: ((~D_PCR.CPC | D_STAT.CIS) & 0x3FF) == 0x3FF [C ps2tek DMAC]
TEST(hw_model_cpcond0) {
    b3native::HwModel m;
    std::vector<std::uint8_t> ram(0x10000, 0), spr(0x4000, 0);
    m.read_ram = [&](std::uint32_t a, void* out, unsigned n) { if (a + n > ram.size()) return false; std::memcpy(out, &ram[a], n); return true; };
    m.write_ram = [&](std::uint32_t a, const void* in, unsigned n) { if (a + n > ram.size()) return false; std::memcpy(&ram[a], in, n); return true; };
    m.scratchpad = [&] { return spr.data(); };
    auto w32 = [&](std::uint32_t a, std::uint32_t v) { std::uint8_t d[8] = {}; std::memcpy(d, &v, 4); m.write(a, 4, d); };
    CHECK(m.cpcond0());                         // no channel selected: set
    w32(0x1000E020u, 1u << 9);                  // D_PCR: CPC for toSPR
    CHECK(!m.cpcond0());                        // CIS9 clear
    w32(0x1000D410u, 0x1000); w32(0x1000D420u, 1); w32(0x1000D480u, 0); w32(0x1000D400u, 0x100);
    CHECK(m.cpcond0());                         // the transfer completed: CIS9 set
    w32(0x1000E010u, 1u << 9);                  // D_STAT: clear CIS9 (write 1)
    CHECK(!m.cpcond0());
    w32(0x1000E020u, 0);
    CHECK(m.cpcond0());
    b3asm::cpcond0_hook() = [] { return true; };
    b3asm::wait_cpcond0(true);                  // returns at once
    b3asm::cpcond0_hook() = nullptr;
    b3asm::wait_cpcond0(false);                 // no model: returns at once
}

// ===== the kernel's objects and threads (docs/specs/ee-kernel.md §2-§5, §9), hand-written
// expectations. The platform's kernel is one per process: these tests run after the ones above and leave no thread
// alive (every thread they make ends dormant and is deleted).
namespace k1 {
namespace p = b3native::platform;
using Row = void (*)(const CallArgs&, CallResult&);
CallResult g_r;
std::int32_t call(Row f, std::initializer_list<std::uint32_t> v) {
    CallArgs a;
    unsigned i = 0;
    for (const std::uint32_t x : v) a.gpr[i++] = detail::gpr64(detail::sext32(x));
    a.n_gpr = i;
    a.sp = backend().stack_pointer();
    CallResult r;   // own: guest threads that run inside the call make calls of their own
    f(a, r);
    g_r = r;
    return static_cast<std::int32_t>(r.v0.w[0]);
}
constexpr gaddr kParams = 0x01000400u;   // ee_thread_t / ee_sema_t blocks
constexpr gaddr kStacks = 0x01800000u;   // guest thread stacks, 0x1000 each
constexpr gaddr kEntryLog = 0x00100100u, kEntryWait = 0x00100104u, kEntrySleep = 0x00100108u, kEntryExit = 0x0010010Cu,
                kEntryThrow = 0x00100110u, kEntrySignal = 0x00100114u;
std::vector<std::int32_t> g_log;   // what the guest threads did, in order
std::int32_t self() { return call(p::get_thread_id, {}); }
void entry_log(const CallArgs&, CallResult& r) { g_log.push_back(self()); r = CallResult{}; }
void entry_wait(const CallArgs& a, CallResult& r) {   // WaitSema(argument): logs id * 100 + result
    const std::int32_t v = call(p::wait_sema, {a.gpr[0].w[0]});
    g_log.push_back(self() * 100 + v);
    r = CallResult{};
}
void entry_sleep(const CallArgs&, CallResult& r) { call(p::sleep_thread, {}); g_log.push_back(self()); r = CallResult{}; }
void entry_exit(const CallArgs&, CallResult& r) {
    g_log.push_back(self());
    call(p::exit_thread, {});
    g_log.push_back(-self());   // never
    r = CallResult{};
}
void entry_throw(const CallArgs&, CallResult&) { p::stub_unexpected("k1 test", "an error on a guest thread"); }
void entry_signal(const CallArgs& a, CallResult& r) {   // SignalSema(argument), then logs its id
    call(p::signal_sema, {a.gpr[0].w[0]});
    g_log.push_back(self());
    r = CallResult{};
}
std::atomic<bool> g_on{true};
std::atomic<std::uint64_t> g_nested{0};
const NativeOverride kEntries[] = {{kEntryLog, &entry_log, 1, &g_on, &g_nested},       {kEntryWait, &entry_wait, 1, &g_on, &g_nested},
                                   {kEntrySleep, &entry_sleep, 1, &g_on, &g_nested},   {kEntryExit, &entry_exit, 1, &g_on, &g_nested},
                                   {kEntryThrow, &entry_throw, 1, &g_on, &g_nested},   {kEntrySignal, &entry_signal, 1, &g_on, &g_nested}};
unsigned g_made = 0;
// CreateThread with an ee_thread_t for `entry` at `prio`
std::int32_t make(gaddr entry, std::uint32_t prio) {
    const gaddr stack = kStacks + 0x1000u * (g_made++ % 64u);
    store<std::uint32_t>(kParams + 4, entry);
    store<std::uint32_t>(kParams + 8, stack);
    store<std::uint32_t>(kParams + 12, 0x1000u);
    store<std::uint32_t>(kParams + 16, 0x004E8670u);
    store<std::uint32_t>(kParams + 20, prio);
    return call(p::create_thread, {kParams});
}
// CreateSema(init, max)
std::int32_t sema(std::uint32_t init, std::uint32_t max) {
    store<std::uint32_t>(kParams + 0x40 + 4, max);
    store<std::uint32_t>(kParams + 0x40 + 8, init);
    store<std::uint32_t>(kParams + 0x40 + 16, 0u);
    store<std::uint32_t>(kParams + 0x40 + 20, 0u);
    return call(p::create_sema, {kParams + 0x40});
}
// ReferThreadStatus's 12 words (empty on -1)
std::vector<std::uint32_t> status(std::int32_t id) {
    if (call(p::refer_thread_status, {static_cast<std::uint32_t>(id), kParams + 0x80}) != 0) return {};
    std::vector<std::uint32_t> w;
    for (unsigned k = 0; k < 12; ++k) w.push_back(load<std::uint32_t>(kParams + 0x80 + 4 * k));
    return w;
}
const b3native::KernelModel::Sema* sema_record(std::int32_t id) {
    for (const auto& s : p::kernel().semas)
        if (s.id == id) return &s;
    return nullptr;
}
std::vector<std::int32_t> ids(const std::vector<b3native::KernelModel::Handler>& l) {
    std::vector<std::int32_t> v;
    for (const auto& h : l) v.push_back(h.id);
    return v;
}
// a backend and the entries for one test
struct Scope {
    NativeBackend nb;
    BackendScope bind{nb};
    Scope() {
        nb.set_stack_pointer(0x01F00000u);
        set_native_overrides(kEntries, sizeof kEntries / sizeof kEntries[0]);
        g_log.clear();
    }
    ~Scope() { set_native_overrides(nullptr, 0); }
};
}  // namespace k1

// §2: ids 1..128 per list, reused; placement by next (0: before the cause's first, -1: after its last, else before
// that id; the end when there is none); remove needs cause and id
TEST(k1_handler_lists) {
    using K = b3native::KernelModel;
    K k;
    CHECK(K::add(k.intc, 2, 0xA0, 0, 0, 0) == 1);
    CHECK(K::add(k.intc, 3, 0xB0, 0, 0, 0) == 2);
    CHECK(K::add(k.intc, 2, 0xC0, 0, 0, 0) == 3);              // before id 1
    CHECK(K::add(k.intc, 2, 0xD0, 0xFFFFFFFFu, 0, 0) == 4);    // after id 1
    CHECK(K::add(k.intc, 5, 0xE0, 2, 0, 0) == 5);              // before id 2, whatever its cause
    CHECK(K::add(k.intc, 6, 0xF0, 99, 0, 0) == 6);             // no id 99: the end
    CHECK(K::add(k.intc, 7, 0x100, 0, 0, 0) == 7);             // no cause 7: the end
    CHECK(K::add(k.intc, 8, 0x110, 0xFFFFFFFFu, 0x5, 0x4E8670) == 8);
    CHECK((k1::ids(k.intc) == std::vector<std::int32_t>{3, 1, 4, 5, 2, 6, 7, 8}));
    CHECK(k.intc.back().handler == 0x110 && k.intc.back().argument == 5 && k.intc.back().gp == 0x4E8670 && k.intc.back().cause == 8);
    CHECK(K::remove(k.intc, 3, 1) == -1);   // id 1 is cause 2's
    CHECK(K::remove(k.intc, 2, 1) == 0);
    CHECK(K::remove(k.intc, 2, 1) == -1);
    CHECK(K::add(k.intc, 9, 0x120, 0, 0, 0) == 1);   // the smallest free id again
    CHECK(K::add(k.dmac, 9, 0x130, 0, 0, 0) == 1);   // the DMAC list has its own ids
    while (k.intc.size() < 128) CHECK(K::add(k.intc, 1, 0x140, 0, 0, 0) > 0);
    CHECK(K::add(k.intc, 1, 0x150, 0, 0, 0) == -1);
    CHECK(k.intc.size() == 128);
}

// §4 / §5.1 records: semaphore ids 0..255, thread ids 2..255, StartThread's checks on the records
TEST(k1_kernel_records) {
    b3native::KernelModel k;
    CHECK(k.create_sema(1, 2, 0, 0) == 0);
    CHECK(k.create_sema(0, 1, 3, 4) == 1);
    CHECK(k.semas[1].count == 0 && k.semas[1].max == 1 && k.semas[1].attr == 3 && k.semas[1].option == 4);
    k.semas.erase(k.semas.begin());
    CHECK(k.create_sema(5, 5, 0, 0) == 0);
    while (k.semas.size() < 256) CHECK(k.create_sema(0, 1, 0, 0) >= 0);
    CHECK(k.create_sema(0, 1, 0, 0) == -1);
    CHECK(k.create_thread(0x100000, 0x200000, 0x1000, 0x4E8670, 10) == 2);
    CHECK(k.create_thread(0x100000, 0x200000, 0x1000, 0x4E8670, 128) == 3);
    CHECK(k.start_thread(2, 0x77) == 2 && k.threads[0].started && k.threads[0].argument == 0x77);
    CHECK(k.start_thread(2, 0x77) == -1);   // started already
    CHECK(k.start_thread(3, 0) == -1);      // priority 128
    CHECK(k.start_thread(9, 0) == -1);      // unknown
}

// §2, §3, §9 through the rows
TEST(k1_kernel_rows) {
    k1::Scope s;
    namespace p = b3native::platform;
    using k1::call;
    // DMAC list: channel > 9 fatal on add, -1 on remove
    const std::int32_t d = call(p::add_dmac_handler, {5, 0x1234, 0, 0x55});
    CHECK(d >= 1 && p::dmac_handler_exists(5));
    CHECK(p::kernel().dmac.back().id == d && p::kernel().dmac.back().argument == 0x55 && p::kernel().dmac.back().gp == 0x004E8670u);
    CHECK_THROWS(call(p::add_dmac_handler, {10, 0x1234, 0, 0}));
    CHECK(call(p::remove_dmac_handler, {10, static_cast<std::uint32_t>(d)}) == -1);
    CHECK(call(p::remove_dmac_handler, {4, static_cast<std::uint32_t>(d)}) == -1);
    CHECK(call(p::remove_dmac_handler, {5, static_cast<std::uint32_t>(d)}) == 0);
    CHECK(!p::dmac_handler_exists(5));
    // INTC list and its copy
    const std::int32_t i = call(p::add_intc_handler, {20, 0x2000, 0, 7});
    bool seen = false;
    for (const auto& h : p::intc_handlers()) seen |= h.id == i && h.cause == 20 && h.handler == 0x2000 && h.argument == 7;
    CHECK(seen);
    CHECK(call(p::remove_intc_handler, {20, static_cast<std::uint32_t>(i)}) == 0);
    CHECK(call(p::remove_intc_handler, {20, static_cast<std::uint32_t>(i)}) == -1);
    // masks: 1 only when the state changes; DMAC's follow into D_STAT's mask half
    call(p::disable_dmac, {5});
    CHECK(call(p::enable_dmac, {5}) == 1);
    CHECK(call(p::enable_dmac, {5}) == 0);
    CHECK((p::kernel().dmac_mask & 0x20u) && (p::hw_model().d_stat() & (0x20u << 16)));
    CHECK(call(p::disable_dmac, {5}) == 1);
    CHECK(call(p::disable_dmac, {5}) == 0);
    CHECK(!(p::hw_model().d_stat() & (0x20u << 16)));
    CHECK_THROWS(call(p::enable_dmac, {10}));
    call(p::disable_intc, {9});
    CHECK(call(p::enable_intc, {9}) == 1);
    CHECK(call(p::enable_intc, {9}) == 0);
    CHECK(p::intc_mask() & 0x200u);
    CHECK(call(p::disable_intc, {9}) == 1);
    CHECK(call(p::disable_intc, {9}) == 0);
    CHECK(!(p::intc_mask() & 0x200u));
    CHECK_THROWS(call(p::enable_intc, {32}));
    // DIntr / EIntr: the previous state
    s.nb.set_interrupts_enabled(true);
    CHECK(call(p::dintr, {}) == 1 && !s.nb.interrupts_enabled());
    CHECK(call(p::dintr, {}) == 0);
    CHECK(call(p::eintr, {}) == 0 && s.nb.interrupts_enabled());
    CHECK(call(p::eintr, {}) == 1);
    // GsPutIMR: the previous mask, 64 bits
    p::hw_model().set_gs_imr(0x7F00u);
    call(p::gs_put_imr, {0xFF00u});
    CHECK(detail::low64(k1::g_r.v0) == 0x7F00u && p::hw_model().gs_imr() == 0xFF00u);
    call(p::gs_put_imr, {0x7F00u});
    // the OSD words
    const gaddr o = 0x01000200u;
    CHECK(call(p::get_osd_config_param, {o}) == 0);
    CHECK(load<std::uint32_t>(o) == p::osd_config_param());
    if (!std::getenv("B3NATIVE_OSD_CONFIG")) CHECK(p::osd_config_param() == 0x00012010u);
    CHECK_THROWS(call(p::get_osd_config_param, {o + 2}));
    for (unsigned k = 0; k < 16; ++k) store<std::uint8_t>(o + 0x10 + k, 0xAA);
    CHECK(call(p::get_osd_config_param2, {o + 0x10, 8, 0}) == 0);
    CHECK(load<std::uint64_t>(o + 0x10) == 0 && load<std::uint64_t>(o + 0x18) == 0xAAAAAAAAAAAAAAAAull);
    CHECK_THROWS(call(p::get_osd_config_param2, {o + 0x10, 257, 0}));
    // _InitSys's TIM3 handler: only into an empty INTC list, id 1
    const auto saved = p::kernel().intc;
    p::kernel().intc.clear();
    p::note_libkernl_timer_handler();
    p::note_libkernl_timer_handler();
    CHECK(p::kernel().intc.size() == 1 && p::kernel().intc[0].id == 1 && p::kernel().intc[0].cause == 11 &&
          p::kernel().intc[0].handler == 0x00115D00u);
    p::kernel().intc = saved;
}

// §4 without waiting: counts, max, PollSema's id, unknown ids, delete, id reuse
TEST(k1_semaphores) {
    k1::Scope s;
    namespace p = b3native::platform;
    using k1::call;
    const std::int32_t a = k1::sema(1, 2);
    CHECK(a >= 0 && k1::sema_record(a) && k1::sema_record(a)->count == 1 && k1::sema_record(a)->max == 2);
    CHECK_THROWS(call(p::create_sema, {k1::kParams + 0x42}));
    CHECK(call(p::wait_sema, {static_cast<std::uint32_t>(a)}) == 0 && k1::sema_record(a)->count == 0);
    CHECK(call(p::poll_sema, {static_cast<std::uint32_t>(a)}) == -1);
    CHECK(call(p::signal_sema, {static_cast<std::uint32_t>(a)}) == 0);
    CHECK(call(p::signal_sema, {static_cast<std::uint32_t>(a)}) == 0);
    CHECK(call(p::signal_sema, {static_cast<std::uint32_t>(a)}) == -1 && k1::sema_record(a)->count == 2);   // at max
    CHECK(call(p::poll_sema, {static_cast<std::uint32_t>(a)}) == a && k1::sema_record(a)->count == 1);
    CHECK(p::kernel_signal_sema(a) && k1::sema_record(a)->count == 2);
    CHECK(!p::kernel_signal_sema(a));   // at max
    CHECK(call(p::wait_sema, {999}) == -1 && call(p::poll_sema, {999}) == -1);
    CHECK(call(p::signal_sema, {999}) == -1 && call(p::delete_sema, {999}) == -1);
    const std::int32_t b = k1::sema(0, 1);
    CHECK(call(p::delete_sema, {static_cast<std::uint32_t>(a)}) == 0);
    CHECK(call(p::wait_sema, {static_cast<std::uint32_t>(a)}) == -1);
    CHECK(k1::sema(0, 1) == a);   // the smallest free id
    call(p::delete_sema, {static_cast<std::uint32_t>(a)});
    call(p::delete_sema, {static_cast<std::uint32_t>(b)});
}

namespace k1 {
using Ids = std::vector<std::int32_t>;
std::uint32_t u(std::int32_t v) { return static_cast<std::uint32_t>(v); }
void start(std::int32_t id, std::uint32_t argument = 0) { CHECK(call(p::start_thread, {u(id), argument}) == id); }
void remove(std::initializer_list<std::int32_t> v) {
    for (const std::int32_t id : v) CHECK(call(p::delete_thread, {u(id)}) == 0);
}
std::int32_t set_main_prio(std::uint32_t prio) { return call(p::change_thread_priority, {0, prio}); }
}  // namespace k1


// §5.3 / §5.4: a higher ready thread runs at once (the preempted one to the front of its queue), equal priorities never
// rotate, FIFO within a priority, a ready thread whose priority changes goes to the back of its new queue
TEST(k1_threads_priority) {
    k1::Scope s;
    namespace p = b3native::platform;
    using namespace k1;
    set_main_prio(10);
    CHECK(p::kernel().main_priority == 10 && call(p::get_thread_id, {}) == 1);
    const std::int32_t t2 = make(kEntryLog, 20), t3 = make(kEntryLog, 20), t4 = make(kEntryLog, 5);
    CHECK(t2 >= 2 && t3 > t2 && t4 > t3);
    const auto st = status(t2);
    CHECK((st == std::vector<std::uint32_t>{0x10, kEntryLog, kStacks + 0x1000u * ((g_made - 3) % 64u), 0x1000, 0x004E8670u,
                                            20, 20, 0, 0, 0, 0, 0}));
    start(t2, 0x77);
    CHECK(status(t2)[0] == 2 && g_log.empty());
    bool rec = false;
    for (const auto& k : p::kernel().threads) rec |= k.id == t2 && k.started && k.argument == 0x77;
    CHECK(rec);
    start(t3);
    CHECK(g_log.empty());
    start(t4);                                   // priority 5 above the main's 10: runs now, ends
    CHECK((g_log == Ids{t4}) && status(t4)[0] == 0x10);
    CHECK(set_main_prio(30) == 10);              // main below the two at 20: they run, FIFO
    CHECK((g_log == Ids{t4, t2, t3}));
    CHECK(call(p::get_thread_id, {}) == 1 && status(0)[0] == 1 && status(0)[6] == 30 && p::kernel().main_priority == 30);
    // equal priorities never rotate: main at 20 waits for a thread at 20 to signal it
    set_main_prio(20);
    g_log.clear();
    const std::int32_t sm = sema(0, 1);
    const std::int32_t t5 = make(kEntrySignal, 20);
    start(t2);
    start(t5, u(sm));
    CHECK(g_log.empty());
    CHECK(call(p::wait_sema, {u(sm)}) == 0);     // t2 ran, then t5 signalled (main ready behind it) and ended
    CHECK((g_log == Ids{t2, t5}));
    // the preempted thread goes to the front: main at 10, t6 at 10 ready, t4 at 5 preempts; main runs before t6
    set_main_prio(10);
    g_log.clear();
    const std::int32_t t6 = make(kEntryLog, 10);
    start(t6);
    start(t4);
    CHECK((g_log == Ids{t4}));
    CHECK(set_main_prio(11) == 10);              // now t6
    CHECK((g_log == Ids{t4, t6}));
    // a ready thread whose priority changes goes to the back of the new queue (an unchanged one stays)
    set_main_prio(10);
    g_log.clear();
    const std::int32_t t7 = make(kEntryLog, 25);
    start(t2);
    start(t3);
    start(t7);
    CHECK(call(p::change_thread_priority, {u(t2), 20}) == 20);   // unchanged: stays first of 20
    CHECK(call(p::change_thread_priority, {u(t3), 25}) == 20);   // queue 25: t7, t3
    CHECK(call(p::change_thread_priority, {u(t2), 25}) == 20);   // queue 25: t7, t3, t2
    CHECK(g_log.empty());
    set_main_prio(30);
    CHECK((g_log == Ids{t7, t3, t2}));
    // no preemption inside a handler or with interrupts disabled; the next check runs it
    set_main_prio(10);
    g_log.clear();
    p::isr_enter();
    start(t4);
    p::isr_exit();
    CHECK(g_log.empty());
    p::maybe_preempt();
    CHECK((g_log == Ids{t4}));
    s.nb.set_interrupts_enabled(false);
    start(t4);
    CHECK((g_log == Ids{t4}));
    s.nb.set_interrupts_enabled(true);
    p::maybe_preempt();
    CHECK((g_log == Ids{t4, t4}));
    // a dormant thread has its initial priority again; dormant / out of range / unknown: -1
    CHECK(status(t2)[6] == 20 && status(t3)[6] == 20);
    CHECK(call(p::change_thread_priority, {u(t2), 3}) == -1);
    CHECK(call(p::change_thread_priority, {0, 128}) == -1);
    CHECK(call(p::change_thread_priority, {200, 3}) == -1);
    remove({t2, t3, t4, t5, t6, t7});
    CHECK(call(p::delete_thread, {u(t2)}) == -1);
    CHECK(make(kEntryLog, 1) == t2);   // the smallest free id
    remove({t2});
    call(p::delete_sema, {u(sm)});
}

// §4 waits: FIFO waiters, a signal readies the first (count unchanged), a delete readies the rest with -1
TEST(k1_threads_waiters) {
    k1::Scope s;
    namespace p = b3native::platform;
    using namespace k1;
    set_main_prio(10);
    const std::int32_t sm = sema(0, 5);
    const std::int32_t t2 = make(kEntryWait, 5), t3 = make(kEntryWait, 5), t4 = make(kEntryWait, 5);
    start(t2, u(sm));
    start(t3, u(sm));
    start(t4, u(sm));
    CHECK(g_log.empty());
    const auto st = status(t3);
    CHECK(st[0] == 4 && st[9] == 2 && st[10] == u(sm));
    CHECK(call(p::poll_sema, {u(sm)}) == -1);
    CHECK(call(p::signal_sema, {u(sm)}) == 0);
    CHECK((g_log == Ids{t2 * 100}) && sema_record(sm)->count == 0);
    CHECK(call(p::delete_sema, {u(sm)}) == 0);
    CHECK((g_log == Ids{t2 * 100, t3 * 100 - 1, t4 * 100 - 1}));
    // with no waiter a signal counts; a thread that then waits takes the count at once
    const std::int32_t s2 = sema(0, 1);
    const std::int32_t t5 = make(kEntryWait, 10);
    start(t5, u(s2));                            // equal to the main: ready, not run
    CHECK(g_log.size() == 3);
    CHECK(call(p::signal_sema, {u(s2)}) == 0 && sema_record(s2)->count == 1);
    set_main_prio(11);
    CHECK(g_log.back() == t5 * 100 && sema_record(s2)->count == 0);
    set_main_prio(10);
    remove({t2, t3, t4, t5});
    call(p::delete_sema, {u(s2)});
}

// §5.1 / §5.2: sleep and wakeup, wakeup counts, terminate (unwinding a parked thread), delete, exit, the -1 cases, an
// error on a guest thread rethrown on the main one
TEST(k1_threads_lifecycle) {
    k1::Scope s;
    namespace p = b3native::platform;
    using namespace k1;
    set_main_prio(10);
    const std::int32_t t2 = make(kEntrySleep, 5);
    start(t2);
    CHECK(g_log.empty());
    const auto st = status(t2);
    CHECK(st[0] == 4 && st[9] == 1 && st[10] == 0);
    CHECK(call(p::wakeup_thread, {u(t2)}) == 0);   // sleeping: ready, runs at once
    CHECK((g_log == Ids{t2}) && status(t2)[0] == 0x10);
    CHECK(call(p::wakeup_thread, {u(t2)}) == -1);  // dormant
    CHECK(call(p::wakeup_thread, {250}) == -1);
    // a wakeup for a thread that is not sleeping is counted; SleepThread uses it up
    CHECK(call(p::wakeup_thread, {1}) == 0 && p::kernel().wakeups == 1 && status(0)[11] == 1);
    CHECK(call(p::sleep_thread, {}) == 0 && p::kernel().wakeups == 0);
    p::isr_enter();
    CHECK_THROWS(call(p::sleep_thread, {}));       // would wait inside a handler
    p::isr_exit();
    // terminate a sleeping thread: dormant, unwound (it never logs), then deletable
    const std::int32_t t3 = make(kEntrySleep, 5);
    start(t3);
    CHECK(call(p::start_thread, {u(t3), 0}) == -1);   // not dormant
    CHECK(call(p::delete_thread, {u(t3)}) == -1);     // not dormant
    CHECK(call(p::terminate_thread, {u(t3)}) == 0);
    CHECK(status(t3)[0] == 0x10 && (g_log == Ids{t2}));
    CHECK(call(p::terminate_thread, {u(t3)}) == -1);  // dormant
    CHECK(call(p::terminate_thread, {1}) == -1);      // the caller
    CHECK(call(p::terminate_thread, {0}) == -1);
    CHECK(call(p::delete_thread, {u(t3)}) == 0 && status(t3).empty());
    CHECK(call(p::delete_thread, {1}) == -1);
    // terminate a thread waiting on a semaphore: out of the waiters (a signal then counts)
    const std::int32_t sm = sema(0, 1);
    const std::int32_t t4 = make(kEntryWait, 5);
    start(t4, u(sm));
    CHECK(call(p::terminate_thread, {u(t4)}) == 0);
    CHECK(call(p::signal_sema, {u(sm)}) == 0 && sema_record(sm)->count == 1 && (g_log == Ids{t2}));
    // a terminated thread starts again on a new host thread
    start(t4, u(sm));                                 // takes the count, logs, ends
    CHECK((g_log == Ids{t2, t4 * 100}) && sema_record(sm)->count == 0);
    // ExitThread: the thread ends there
    const std::int32_t t5 = make(kEntryExit, 5);
    start(t5);
    CHECK((g_log == Ids{t2, t4 * 100, t5}) && status(t5)[0] == 0x10);
    CHECK_THROWS(call(p::exit_thread, {}));        // the main thread
    // priority out of range at start
    const std::int32_t t6 = make(kEntryLog, 128);
    CHECK(t6 > 0 && call(p::start_thread, {u(t6), 0}) == -1);
    CHECK(call(p::start_thread, {250, 0}) == -1);
    // an error on a guest thread ends the run on the main thread
    const std::int32_t t7 = make(kEntryThrow, 5);
    CHECK_THROWS(call(p::start_thread, {u(t7), 0}));
    CHECK(call(p::get_thread_id, {}) == 1 && status(0)[0] == 1 && status(t7)[0] == 0x10);
    // WaitSema with interrupts disabled cannot wait
    const std::int32_t s3 = sema(0, 1);
    s.nb.set_interrupts_enabled(false);
    CHECK_THROWS(call(p::wait_sema, {u(s3)}));
    s.nb.set_interrupts_enabled(true);
    CHECK(status(0)[0] == 1);
    remove({t2, t4, t5, t6, t7});
    call(p::delete_sema, {u(sm)});
    call(p::delete_sema, {u(s3)});
}

// ---- the kernel's time (docs/specs/ee-kernel.md §6-§8): virtual time, alarms, delivery order, the DMAC completion
// queue, with hand-made expectations from the spec
namespace {
struct K2Event { char kind; std::uint32_t a0, a1, a2; std::uint64_t at; };
std::vector<K2Event>* g_k2 = nullptr;
void k2_alarm(const CallArgs& a, CallResult& r) {
    if (g_k2) g_k2->push_back({'a', a.gpr[0].w[0], a.gpr[1].w[0], a.gpr[2].w[0], b3native::platform::virtual_cycles()});
    r = CallResult{};
}
void k2_dmac(const CallArgs& a, CallResult& r) {
    if (g_k2) g_k2->push_back({'d', a.gpr[0].w[0], a.gpr[1].w[0], static_cast<std::uint32_t>(b3native::platform::isr_depth()),
                               b3native::platform::virtual_cycles()});
    r = CallResult{};
}
std::atomic<bool> g_k2_on{true};
std::atomic<std::uint64_t> g_k2_nested{0};
constexpr gaddr kK2Alarm = 0x00100010u, kK2Dmac = 0x00100014u;
CallArgs k2_args(std::uint32_t a0, std::uint32_t a1 = 0, std::uint32_t a2 = 0, std::uint32_t a3 = 0) {
    CallArgs a;
    a.gpr[0] = detail::gpr64(a0);
    a.gpr[1] = detail::gpr64(a1);
    a.gpr[2] = detail::gpr64(a2);
    a.gpr[3] = detail::gpr64(a3);
    return a;
}
std::int32_t k2_v0(const CallResult& r) { return static_cast<std::int32_t>(detail::low64(r.v0)); }
std::vector<K2Event>* g_k2_hook = nullptr;
}  // namespace

TEST(k2_alarm_ids_and_release) {
    NativeBackend nb;
    BackendScope scope(nb);
    namespace p = b3native::platform;
    CallResult r;
    p::set_alarm(k2_args(5, 0, 0), r);
    CHECK(k2_v0(r) == -1);                                  // handler 0
    std::vector<std::int32_t> ids;
    for (std::uint32_t k = 0; k < 64; ++k) { p::set_alarm(k2_args(1000, kK2Alarm, k), r); ids.push_back(k2_v0(r)); }
    CHECK(ids.front() == 1 && ids.back() == 64);
    p::set_alarm(k2_args(1000, kK2Alarm, 0), r);
    CHECK(k2_v0(r) == -1);                                  // none free
    p::release_alarm(k2_args(7), r);
    CHECK(k2_v0(r) == 0);
    p::release_alarm(k2_args(7), r);
    CHECK(k2_v0(r) == -1);                                  // not pending any more
    p::set_alarm(k2_args(1000, kK2Alarm, 0), r);
    CHECK(k2_v0(r) == 7);                                   // the lowest free id again
    for (std::uint32_t id = 1; id <= 64; ++id) { p::release_alarm(k2_args(id), r); CHECK(k2_v0(r) == 0); }
    p::release_alarm(k2_args(65), r);
    CHECK(k2_v0(r) == -1);
}

TEST(k2_alarm_order_and_vblank) {
    NativeBackend nb;
    BackendScope scope(nb);
    namespace p = b3native::platform;
    NativeOverride table[] = {{kK2Alarm, &k2_alarm, 0, &g_k2_on, &g_k2_nested}};
    set_native_overrides(table, 1);
    nb.set_stack_pointer(0x01F00000u);
    std::vector<K2Event> ev;
    g_k2 = &ev;
    p::add_vblank_hook([] { if (g_k2_hook) g_k2_hook->push_back({'v', 0, 0, 0, b3native::platform::virtual_cycles()}); });
    g_k2_hook = &ev;
    p::vblank();                                             // align on a VBlank start: the clock jumps to it
    const std::uint64_t v0 = p::virtual_cycles();
    CHECK(v0 % 4915200u == 0 && p::next_vblank_start(v0 + 1) == v0 + 4915200u && p::next_vblank_start(v0) == v0);
    CHECK(p::next_vblank_start(v0 + 4915201u) == v0 + 2 * 4915200u);
    ev.clear();
    CallResult r;
    // equal deadlines: the earlier set first; a shorter one before both; ticks 0 counts as 1; the low 16 bits only
    p::set_alarm(k2_args(2, kK2Alarm, 0xA), r);
    const std::int32_t a = k2_v0(r);
    p::set_alarm(k2_args(0x10002, kK2Alarm, 0xB), r);
    const std::int32_t b = k2_v0(r);
    p::set_alarm(k2_args(0, kK2Alarm, 0xC), r);
    const std::int32_t c = k2_v0(r);
    nb.add_cost(18750 - 1);
    p::time_check();
    CHECK(ev.empty());                                       // nothing due yet
    nb.add_cost(1);
    p::time_check();
    CHECK(ev.size() == 1 && ev[0].a0 == static_cast<std::uint32_t>(c) && ev[0].a1 == 0 && ev[0].a2 == 0xC);
    nb.add_cost(18750 + 5);
    p::time_check();
    CHECK(ev.size() == 3 && ev[1].a0 == static_cast<std::uint32_t>(a) && ev[1].a1 == 2 && ev[1].a2 == 0xA &&
          ev[2].a0 == static_cast<std::uint32_t>(b) && ev[2].a1 == 2 && ev[2].a2 == 0xB);
    p::release_alarm(k2_args(a), r);
    CHECK(k2_v0(r) == -1);                                   // removed before its call
    // an alarm whose deadline is the next VBlank start: the alarm first, then the VBlank
    ev.clear();
    const std::uint64_t now = p::virtual_cycles();
    const std::uint64_t vb = p::next_vblank_start(now + 1);
    nb.add_cost(static_cast<std::uint32_t>((vb - now) % 18750u));
    p::set_alarm(k2_args(static_cast<std::uint32_t>((vb - p::virtual_cycles()) / 18750u), kK2Alarm, 0xD), r);
    const std::uint64_t vblanks = p::vblank_count();
    nb.add_cost(static_cast<std::uint32_t>(vb - p::virtual_cycles()));
    CHECK(p::virtual_cycles() == vb);
    p::time_check();
    CHECK(ev.size() == 2 && ev[0].kind == 'a' && ev[0].a2 == 0xD && ev[0].at == vb && ev[1].kind == 'v' && ev[1].at == vb);
    CHECK(p::vblank_count() == vblanks + 1);
    // vblank(): an alarm before the VBlank is delivered at its deadline (the clock jumps), then the VBlank
    ev.clear();
    const std::uint64_t t1 = p::virtual_cycles();
    p::set_alarm(k2_args(3, kK2Alarm, 0xE), r);
    p::vblank();
    CHECK(ev.size() == 2 && ev[0].kind == 'a' && ev[0].at == t1 + 3 * 18750u && ev[1].kind == 'v' &&
          ev[1].at == vb + 4915200u);
    // with interrupts disabled time_check delivers a due alarm but no VBlank; vblank() is fatal
    ev.clear();
    p::set_alarm(k2_args(1, kK2Alarm, 0xF), r);
    nb.add_cost(4915200);
    nb.set_interrupts_enabled(false);
    p::time_check();
    CHECK(ev.size() == 1 && ev[0].kind == 'a' && ev[0].a2 == 0xF);
    CHECK_THROWS(p::vblank());
    nb.set_interrupts_enabled(true);
    p::time_check();                                         // the late VBlank now
    CHECK(ev.size() == 2 && ev[1].kind == 'v');
    g_k2_hook = nullptr;
    g_k2 = nullptr;
    set_native_overrides(nullptr, 0);
}

TEST(k2_dmac_interrupt_delivery) {
    NativeBackend nb;
    BackendScope scope(nb);
    namespace p = b3native::platform;
    NativeOverride table[] = {{kK2Dmac, &k2_dmac, 0, &g_k2_on, &g_k2_nested}};
    set_native_overrides(table, 1);
    nb.set_stack_pointer(0x01F00000u);
    std::vector<K2Event> ev;
    g_k2 = &ev;
    CallResult r;
    p::add_dmac_handler(k2_args(5, kK2Dmac, 0, 0x55), r);
    const std::int32_t h1 = k2_v0(r);
    p::add_dmac_handler(k2_args(5, kK2Dmac, 0xFFFFFFFFu, 0x66), r);
    const std::int32_t h2 = k2_v0(r);
    nb.set_interrupts_enabled(false);
    CHECK(!p::dmac_interrupt(5));                            // interrupts off: not delivered
    CHECK(ev.empty());
    nb.set_interrupts_enabled(true);
    CHECK(p::dmac_interrupt(5));
    // list order, in interrupt context; interrupts enabled afterwards
    CHECK(ev.size() == 2 && ev[0].a0 == 5 && ev[0].a1 == 0x55 && ev[0].a2 == 1 && ev[1].a1 == 0x66 && ev[1].a2 == 1);
    CHECK(nb.interrupts_enabled() && p::isr_depth() == 0);
    p::remove_dmac_handler(k2_args(5, static_cast<std::uint32_t>(h1)), r);
    p::remove_dmac_handler(k2_args(5, static_cast<std::uint32_t>(h2)), r);
    g_k2 = nullptr;
    set_native_overrides(nullptr, 0);
}

TEST(k2_hw_model_queue_rules) {
    CHECK(b3native::HwModel::completion_delay(0) == 32 && b3native::HwModel::completion_delay(7) == 32);
    CHECK(b3native::HwModel::completion_delay(8) == 0 && b3native::HwModel::completion_delay(9) == 0);
    auto w32 = [](b3native::HwModel& m, std::uint32_t a, std::uint32_t v) { std::uint8_t d[8] = {}; std::memcpy(d, &v, 4); m.write(a, 4, d); };
    {   // without a clock: the lowest channel whose status and mask bits are both set, one at a time
        b3native::HwModel m;
        w32(m, 0x1000A000u, 0x101);   // ch2
        w32(m, 0x10009000u, 0x101);   // ch1
        std::vector<unsigned> got;
        m.on_dmac_interrupt = [&](unsigned c) { got.push_back(c); m.ack_dmac(c); return true; };
        m.set_dmac_mask((1u << 1) | (1u << 2));
        CHECK(got.size() == 2 && got[0] == 1 && got[1] == 2);
        CHECK(m.queued_completions() == 0 && m.next_completion_ready() == ~std::uint64_t{0});
    }
    {   // set_dmac_mask: the mask half only
        b3native::HwModel m;
        w32(m, 0x1000A000u, 0x101);
        m.set_dmac_mask(1u << 3);
        CHECK(m.d_stat() == ((1u << 19) | 4u));
    }
    {   // with a clock: a channel without a handler stays pending while later ones pass; one entry per channel;
        // a refused channel keeps its place
        b3native::HwModel m;
        std::uint64_t now = 100;
        m.clock = [&] { return now; };
        std::vector<unsigned> got;
        bool allow = true;
        bool handler2 = false;
        m.has_dmac_handler = [&](unsigned c) { return c != 2 || handler2; };
        m.on_dmac_interrupt = [&](unsigned c) { if (!allow) return false; got.push_back(c); m.ack_dmac(c); return true; };
        m.set_dmac_mask((1u << 0) | (1u << 1) | (1u << 2));
        w32(m, 0x1000A000u, 0x101);   // ch2, ready 132
        w32(m, 0x1000A000u, 0x101);   // ch2 again: one pending entry
        w32(m, 0x10009000u, 0x101);   // ch1
        CHECK(m.queued_completions() == 3 && m.next_completion_ready() == 132);
        now = 132; m.poll_interrupts();
        CHECK(got.size() == 1 && got[0] == 1 && m.pending_dmac().size() == 1 && m.pending_dmac()[0] == 2);
        allow = false;
        w32(m, 0x10008000u, 0x100);   // ch0 (normal mode, no VIF0 owner), ready 164
        now = 164; m.poll_interrupts();
        CHECK(m.pending_dmac().size() == 2 && m.pending_dmac()[0] == 2 && m.pending_dmac()[1] == 0);
        handler2 = true;
        m.poll_interrupts();          // refused: ch2 keeps the first place
        CHECK(m.pending_dmac().size() == 2 && m.pending_dmac()[0] == 2 && got.size() == 1);
        allow = true;
        m.poll_interrupts();
        CHECK(got.size() == 3 && got[1] == 2 && got[2] == 0 && m.pending_dmac().empty());
        // rebase: the remaining delay kept on another clock
        w32(m, 0x1000A000u, 0x101);   // ready 196
        m.rebase_completions(170, 1000);
        CHECK(m.next_completion_ready() == 1026);
        m.rebase_completions(2000, 5);   // already due: ready at the new clock
        CHECK(m.next_completion_ready() == 5);
    }
    {   // SPR DMA: the scratchpad address wraps within 16 KB; MADR at or above 0x10000000 is fatal
        b3native::HwModel m;
        std::vector<std::uint8_t> ram(0x10000, 0), spr(0x4000, 0);
        for (unsigned k = 0; k < 32; ++k) ram[0x1000 + k] = static_cast<std::uint8_t>(0x80 + k);
        m.read_ram = [&](std::uint32_t a, void* out, unsigned n) { if (a + n > ram.size()) return false; std::memcpy(out, &ram[a], n); return true; };
        m.write_ram = [&](std::uint32_t a, const void* in, unsigned n) { if (a + n > ram.size()) return false; std::memcpy(&ram[a], in, n); return true; };
        m.scratchpad = [&] { return spr.data(); };
        w32(m, 0x1000D410u, 0x1004);  // MADR: its low 4 bits ignored for the copy
        w32(m, 0x1000D420u, 2);
        w32(m, 0x1000D480u, 0x3FF0);
        w32(m, 0x1000D400u, 0x100);
        CHECK(std::memcmp(&spr[0x3FF0], &ram[0x1000], 16) == 0 && std::memcmp(&spr[0], &ram[0x1010], 16) == 0);
        CHECK(m.chan(9, 8) == 0x0010 && m.chan(9, 1) == 0x1024);
        w32(m, 0x1000D410u, 0x10000000u);
        w32(m, 0x1000D420u, 1);
        CHECK_THROWS(w32(m, 0x1000D400u, 0x100));
    }
}

TEST_MAIN("native platform tier: C runtime vs retail")
