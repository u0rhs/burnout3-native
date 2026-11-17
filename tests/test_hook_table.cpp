// The hook table and the mod arena (src/native/hook_table.h; mod kit Phase 2a, docs/modding/DESIGN.md §2.3 / §2.4):
// layer order, next-chaining, all-or-nothing installs, replace / replace conflicts, seal, switch-off, bound hooks,
// dispatch through the published table, config/mod_hooks.txt's route prologue (b3host/hook_route.h), the arena's
// determinism and bounds; online_message.h's swap of the ONLINE dialog's text; speed_units.h's units word around the
// display functions. Synthetic rows only (no game code; the
// disc's string 105 is read from $B3_GAME_DIR at test time when it is there, never kept).
#include "b3host/call.h"
#include "b3host/hook_route.h"
#include "b3host/native_backend.h"
#include "hook_table.h"
#include "../tools/native/online_message.h"
#include "../tools/native/speed_units.h"
#include "test.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace b3host;
using b3native::GuestArena;
using b3native::Hook;
using b3native::HookEntry;
using b3native::HookLayer;
using b3native::HookTable;
using b3native::TableEntry;

namespace {

constexpr gaddr kA = 0x00100000u, kB = 0x00100010u, kPlat = 0x00100020u, kR = 0x00100100u;

std::string g_log;   // which layers ran, top first
int gen_a(int x) { g_log += "gen "; return x * 3; }
int gen_b(int x) { g_log += "genB "; return x + 100; }
int plat(int x) { g_log += "plat "; return x; }

int g_body = 0, g_hook_calls = 0;
// a definition as decomp2host writes it for an address in config/mod_hooks.txt
extern "C" int routed_def(int x) {
    b3host::RouteScope b3_route_scope;   // decomp2host: config/mod_hooks.txt (mod kit 2a)
    if (b3host::route_call<kR>(b3_route_scope)) return b3host::call_guest<int>(kR, b3host::stub_arg<decltype(x)>(x));
    ++g_body;
    return x > 0 ? routed_def(x - 1) + 1 : 0;   // a direct recursive call: a hook sees it too
}

std::atomic<bool> g_on{true};
std::atomic<std::uint64_t> g_nested{0};
const NativeOverride kRows[] = {
    {kA, &traced_entry<kA, &native_entry<&gen_a>>, 4, &g_on, &g_nested},
    {kB, &traced_entry<kB, &native_entry<&gen_b>>, 4, &g_on, &g_nested},
    {kPlat, &traced_entry<kPlat, &native_entry<&plat>>, 0, &g_on, &g_nested},
    {kR, &traced_entry<kR, &native_entry<&routed_def>>, 4, &g_on, &g_nested},
};
const TableEntry kEntries[] = {
    {kA, "leaf_a", "game:matched", 'i'},
    {kB, "leaf_b", "game:matched", 'i'},
    {kPlat, "sceThing", "platform", 'i'},
    {kR, "routed_def", "game:matched", 'i'},
};
constexpr std::size_t kN = sizeof kRows / sizeof kRows[0];

CallArgs args(std::uint32_t a0) {
    CallArgs c;
    c.gpr[0] = detail::gpr64(a0);
    c.n_gpr = c.n_fpr = 8;
    c.sp = 0x01F00000u;
    return c;
}
int run(HookEntry e, std::uint32_t a0) {
    CallResult r;
    e(args(a0), r);
    return static_cast<int>(r.v0.w[0]);
}

// Each layer logs its name, calls next with a0 + 1 and adds 1000 to v0 (so the result shows how many layers ran).
template <int Id>
struct Layer {
    static inline HookEntry next = nullptr;
    static inline const char* name = "";
    static void fn(const CallArgs& a, CallResult& r) {
        g_log += name;
        g_log += " ";
        CallArgs b = a;
        b.gpr[0] = detail::gpr64(a.gpr[0].w[0] + 1);
        if (next) next(b, r);
        r.v0.w[0] += 1000;
    }
    static Hook at(gaddr addr, HookLayer l, const char* n) {
        name = n;
        return {addr, l, &fn, &next};
    }
};
// a replacement that never calls next
void replace_only(const CallArgs&, CallResult& r) { g_log += "only "; r.v0 = u_long128{}; r.v0.w[0] = 7; }

struct Published {   // a NativeBackend for call_guest, and the table cleared afterwards
    NativeBackend nb;
    BackendScope scope{nb};
    Published() { nb.set_stack_pointer(0x01F00000u); }
    ~Published() { set_native_overrides(nullptr, 0); }
};

}  // namespace

TEST(layer_order_is_fixed_whatever_the_install_order) {
    HookTable t;
    CHECK(t.attach_rows(kRows, kEntries, kN, false));
    std::string e;
    // installed out of order: port a, mod wrap x, mod replace, port b, mod wrap y
    CHECK(t.install("port:a", {Layer<1>::at(kA, HookLayer::kPort, "portA")}, &e));
    CHECK(t.install("mod:x", {Layer<2>::at(kA, HookLayer::kWrap, "wrapX")}, &e));
    CHECK(t.install("mod:r", {Layer<3>::at(kA, HookLayer::kReplace, "replace")}, &e));
    CHECK(t.install("port:b", {Layer<4>::at(kA, HookLayer::kPort, "portB")}, &e));
    CHECK(t.install("mod:y", {Layer<5>::at(kA, HookLayer::kWrap, "wrapY")}, &e));
    g_log.clear();
    // top first: the first mod in the load order, the next mod, the port's wrappers (last installed on top), the
    // mod's replacement, the generated function
    const int v = run(t.top(kA), 1);
    CHECK(g_log == "wrapX wrapY portB portA replace gen ");
    CHECK(v == (1 + 5) * 3 + 5 * 1000);   // each layer passed a0 + 1 down and added 1000 on the way up
    const std::vector<HookTable::Link> c = t.chain(kA);
    CHECK(c.size() == 6);
    CHECK(c[0].layer == HookLayer::kGenerated && c[0].owner == "game:matched" && c[0].fn == kRows[0].entry);
    CHECK(c[1].layer == HookLayer::kReplace && c[1].owner == "mod:r");
    CHECK(c[2].owner == "port:a" && c[3].owner == "port:b");
    CHECK(c[4].owner == "mod:y" && c[5].owner == "mod:x" && c[5].layer == HookLayer::kWrap);
    CHECK(t.top(kA) == &Layer<2>::fn);
    // every next is the layer below
    CHECK(Layer<3>::next == kRows[0].entry && Layer<1>::next == &Layer<3>::fn && Layer<4>::next == &Layer<1>::fn &&
          Layer<5>::next == &Layer<4>::fn && Layer<2>::next == &Layer<5>::fn);
    // an address without hooks keeps its row's entry (the no-hook fast path is the row itself)
    CHECK(t.top(kB) == kRows[1].entry && !t.hooked(kB) && t.hooked(kA));
    CHECK(t.hooked_addresses() == std::vector<gaddr>{kA});
}

TEST(port_layer_keeps_the_chaining_of_today) {
    // the port's wrappers used to copy the table and chain on what was installed: the same order
    HookTable t;
    CHECK(t.attach_rows(kRows, kEntries, kN, false));
    CHECK(t.install("port:first", {Layer<11>::at(kB, HookLayer::kPort, "first")}));
    CHECK(t.install("port:second", {Layer<12>::at(kB, HookLayer::kPort, "second")}));
    g_log.clear();
    CHECK(run(t.top(kB), 0) == 2 + 100 + 2000);
    CHECK(g_log == "second first genB ");
}

TEST(a_replacement_may_skip_next) {
    HookTable t;
    CHECK(t.attach_rows(kRows, kEntries, kN, false));
    CHECK(t.install("mod:only", {{kA, HookLayer::kReplace, &replace_only, nullptr}}));
    CHECK(t.install("port:w", {Layer<21>::at(kA, HookLayer::kPort, "portW")}));
    g_log.clear();
    CHECK(run(t.top(kA), 1) == 1007);
    CHECK(g_log == "portW only ");   // the generated function did not run
    CHECK(Layer<21>::next == &replace_only);
}

TEST(install_is_all_or_nothing) {
    HookTable t;
    CHECK(t.attach_rows(kRows, kEntries, kN, false));
    std::string e;
    CHECK(t.install("port:ok", {Layer<31>::at(kA, HookLayer::kPort, "ok")}, &e));
    const HookEntry before_a = t.top(kA), before_b = t.top(kB);
    // a set whose last hook has no row: none of it goes in
    CHECK(!t.install("mod:half", {Layer<32>::at(kB, HookLayer::kWrap, "w"), Layer<33>::at(kA, HookLayer::kReplace, "r"),
                                  Layer<34>::at(0x00100004u, HookLayer::kWrap, "x")}, &e));
    CHECK(e.find("0x00100004 has no native table entry") != std::string::npos);
    CHECK(t.top(kA) == before_a && t.top(kB) == before_b && !t.hooked(kB) && t.chain(kA).size() == 2);
    CHECK(t.owners() == std::vector<std::string>{"port:ok"});
    // the same set without the bad address installs
    CHECK(t.install("mod:half", {Layer<32>::at(kB, HookLayer::kWrap, "w"), Layer<33>::at(kA, HookLayer::kReplace, "r")}, &e));
    CHECK(t.hooked(kB) && t.chain(kA).size() == 3);
    // one owner, one set
    CHECK(!t.install("mod:half", {Layer<35>::at(kB, HookLayer::kWrap, "w2")}, &e) && e.find("already installed") != std::string::npos);
    // a hook without a function, an empty set, layer 0
    CHECK(!t.install("mod:null", {{kB, HookLayer::kWrap, nullptr, nullptr}}, &e));
    CHECK(!t.install("mod:empty", std::vector<Hook>{}, &e));
    CHECK(!t.install("mod:gen", {{kB, HookLayer::kGenerated, &replace_only, nullptr}}, &e));
    CHECK(t.owners().size() == 2);
}

TEST(platform_tier_refused_to_mods_unless_allowed) {
    HookTable t;
    CHECK(t.attach_rows(kRows, kEntries, kN, false));
    std::string e;
    CHECK(!t.install("mod:p", {Layer<41>::at(kPlat, HookLayer::kWrap, "w")}, &e));
    CHECK(e.find("platform tier") != std::string::npos && !t.hooked(kPlat));
    CHECK(t.install("mod:p", {Layer<41>::at(kPlat, HookLayer::kWrap, "w")}, &e, true));   // [code] platform = yes + Developer mods
    CHECK(t.install("port:trace", {Layer<42>::at(kPlat, HookLayer::kPort, "t")}, &e));    // the port's own layer may
}

TEST(replace_replace_is_a_conflict_naming_both) {
    HookTable t;
    CHECK(t.attach_rows(kRows, kEntries, kN, false));
    std::string e;
    CHECK(t.install("mod:first", {Layer<51>::at(kA, HookLayer::kReplace, "r1")}, &e));
    const HookEntry before_b = t.top(kB);
    CHECK(!t.install("mod:second", {Layer<52>::at(kB, HookLayer::kWrap, "w"), Layer<53>::at(kA, HookLayer::kReplace, "r2")}, &e));
    CHECK(e == "mod:second conflicts with mod:first: both replace 0x00100000 leaf_a");
    CHECK(t.top(kB) == before_b && !t.hooked(kB));   // the loser's other hook did not go in either
    // two replaces of one address in one set
    CHECK(!t.install("mod:twice", {Layer<54>::at(kB, HookLayer::kReplace, "a"), Layer<55>::at(kB, HookLayer::kReplace, "b")}, &e));
    CHECK(e.find("replaces 0x00100010 leaf_b twice") != std::string::npos);
    // wraps chain without limit
    CHECK(t.install("mod:w1", {Layer<56>::at(kA, HookLayer::kWrap, "w1")}, &e));
    CHECK(t.install("mod:w2", {Layer<57>::at(kA, HookLayer::kWrap, "w2")}, &e));
    g_log.clear();
    run(t.top(kA), 0);
    CHECK(g_log == "w1 w2 r1 gen ");
}

TEST(sealed_table_takes_nothing) {
    HookTable t;
    std::string e;
    CHECK(!t.install("port:early", {Layer<61>::at(kA, HookLayer::kPort, "e")}, &e));   // not attached
    CHECK(t.attach_rows(kRows, kEntries, kN, false));
    CHECK(!t.attach_rows(kRows, kEntries, kN, false));   // once
    t.seal();
    CHECK(!t.install("port:late", {Layer<61>::at(kA, HookLayer::kPort, "l")}, &e) && e.find("sealed") != std::string::npos);
    CHECK(!t.hooked(kA));
}

TEST(attach_checks_rows_against_entries) {
    HookTable t;
    TableEntry swapped[kN];
    for (std::size_t k = 0; k < kN; ++k) swapped[k] = kEntries[k];
    std::swap(swapped[0], swapped[1]);
    CHECK(!t.attach_rows(kRows, swapped, kN, false));
    CHECK(!t.attach_rows(kRows, kEntries, 0, false));
}

namespace {
struct Counter { int calls = 0; int add = 0; };
void counting(const CallArgs& a, CallResult& r, void* user, HookEntry next) {
    Counter* c = static_cast<Counter*>(user);
    ++c->calls;
    next(a, r);
    r.v0.w[0] += static_cast<std::uint32_t>(c->add);
}
}  // namespace

TEST(bound_hooks_carry_their_data) {
    HookTable t;
    CHECK(t.attach_rows(kRows, kEntries, kN, false));
    Counter c1{0, 10}, c2{0, 20};
    const unsigned used = b3native::bound_slots_used();
    const Hook h1 = b3native::bind_hook(kA, HookLayer::kWrap, &counting, &c1);
    const Hook h2 = b3native::bind_hook(kA, HookLayer::kWrap, &counting, &c2);
    CHECK(b3native::bound_slots_used() == used + 2 && h1.fn != h2.fn && h1.fn && h1.next);
    CHECK(t.install("mod:c1", {h1}));
    CHECK(t.install("mod:c2", {h2}));
    g_log.clear();
    CHECK(run(t.top(kA), 2) == 6 + 30);
    CHECK(c1.calls == 1 && c2.calls == 1 && g_log == "gen ");
    CHECK(!b3native::bind_hook(kA, HookLayer::kWrap, nullptr, nullptr).fn);
}

TEST(published_table_is_what_dispatch_runs) {
    Published p;
    HookTable t;
    CHECK(t.attach_rows(kRows, kEntries, kN, true));
    // no hooks: the rows' own entries (nothing in between)
    CHECK(native_override(kA) != nullptr && native_override(kA)->entry == kRows[0].entry);
    g_log.clear();
    CHECK(call_guest<int>(kA, 2) == 6 && g_log == "gen ");
    CHECK(t.install("port:p", {Layer<71>::at(kA, HookLayer::kPort, "p")}));
    CHECK(native_override(kA)->entry == &Layer<71>::fn && native_override(kA)->cost == 4);   // the row's cost stays
    g_log.clear();
    CHECK(call_guest<int>(kA, 2) == 9 + 1000 && g_log == "p gen ");
    // switch-off (explore_standins.h RetailFor): native_override answers none
    std::atomic<bool> off{false};
    std::string e;
    CHECK(!t.switch_off("port:retail-for", {0x00100008u}, &off, &e));
    CHECK(native_override(kB) != nullptr);
    CHECK(t.switch_off("port:retail-for", {kB}, &off, &e));
    CHECK(native_override(kB) == nullptr && native_override(kA) != nullptr);
}

namespace {
struct H {
    static inline HookEntry next = nullptr;
    static void fn(const CallArgs& a, CallResult& r) { ++g_hook_calls; next(a, r); }
};
}  // namespace

TEST(route_prologue_sends_direct_calls_through_the_hooks) {
    Published p;
    HookTable t;
    CHECK(t.attach_rows(kRows, kEntries, kN, true));
    g_body = 0;
    // no hook: a direct call runs the body only (four entries: 3, 2, 1, 0)
    CHECK(routed_def(3) == 3 && g_body == 4);
    CHECK(!route_hooked(kR));
    CHECK(t.install("port:count", {{kR, HookLayer::kPort, &H::fn, &H::next}}));
    CHECK(route_hooked(kR));
    g_body = g_hook_calls = 0;
    // a direct call (and each direct recursive call in the body) goes through dispatch: the hook sees all four
    CHECK(routed_def(3) == 3);
    CHECK(g_hook_calls == 4 && g_body == 4);
    // a dispatched call: once per entry as well, never twice
    g_body = g_hook_calls = 0;
    CHECK(call_guest<int>(kR, 2) == 2);
    CHECK(g_hook_calls == 3 && g_body == 3);
    CHECK(t_route_taken == 0 && native_chain::t_depth == 0);
}

TEST(route_bits_only_for_a_published_table) {
    HookTable t;
    CHECK(t.attach_rows(kRows, kEntries, kN, false));
    CHECK(t.install("port:x", {Layer<81>::at(kR, HookLayer::kPort, "x")}));
    CHECK(!route_hooked(kR));
}

TEST(arena_port_regions_are_where_they_always_were) {
    // pc_menu.h, date_format.h, custom_soundtrack.h: the addresses before Phase 2a
    CHECK(GuestArena::kPcMenu.address == 0x000C0000u && GuestArena::kPcMenu.size == 0x400u);
    CHECK(GuestArena::kDateFormat.address == 0x000C0800u && GuestArena::kDateFormat.size == 0x20u);
    CHECK(GuestArena::kCustomSoundtrack.address == 0x000C0C00u && GuestArena::kCustomSoundtrack.size == 0x300u);
    CHECK(GuestArena::kNowPlaying.address == 0x000C0F00u && GuestArena::kNowPlaying.size == 0x300u);   // now_playing_popup.h
    CHECK(GuestArena::kOnlineMessage.address == 0x000C1200u && GuestArena::kOnlineMessage.size == 0x100u);   // online_message.h
    CHECK(GuestArena::kEnd == 0x00100000u);   // the ELF's first PT_LOAD
    CHECK(b3native::arena_port_regions_ok());
}

TEST(arena_is_deterministic_and_bounded) {
    auto sequence = [](GuestArena& a) {
        std::vector<gaddr> v;
        v.push_back(a.alloc("mod:a", 10, 4));
        v.push_back(a.alloc("mod:a", 100, 64));
        v.push_back(a.alloc("mod:b", 1, 1));
        v.push_back(a.alloc("mod:b", 0x800, 0x1000));
        return v;
    };
    GuestArena a1, a2;
    const std::vector<gaddr> s1 = sequence(a1), s2 = sequence(a2);
    CHECK(s1 == s2);
    CHECK(s1[0] == GuestArena::kPortEnd && s1[1] == GuestArena::kPortEnd + 0x40u && s1[2] == GuestArena::kPortEnd + 0x40u + 100u &&
          s1[3] == GuestArena::kPortEnd + 0x1000u);
    CHECK(a1.allocations().size() == 4 && a1.allocations()[2].owner == "mod:b");
    for (gaddr g : s1) CHECK(g >= GuestArena::kPortEnd && g < GuestArena::kEnd);   // never in the port's block
    std::string e;
    // refusals change nothing
    const gaddr at = a1.next();
    CHECK(a1.alloc("mod:c", 0, 4, &e) == 0);
    CHECK(a1.alloc("mod:c", 4, 3, &e) == 0 && e.find("alignment") != std::string::npos);
    CHECK(a1.alloc("mod:c", 4, 8192, &e) == 0);
    CHECK(a1.alloc("", 4, 4, &e) == 0);
    CHECK(a1.alloc("mod:c", GuestArena::kEnd, 4, &e) == 0 && e.find("do not fit") != std::string::npos);
    CHECK(a1.alloc("mod:c", 0xFFFFFFFFu, 1, &e) == 0);
    CHECK(a1.next() == at && a1.allocations().size() == 4);
    // up to the last byte below the ELF, then nothing
    const std::uint32_t left = GuestArena::kEnd - a1.next();
    CHECK(a1.alloc("mod:c", left, 1, &e) == GuestArena::kEnd - left);
    CHECK(a1.next() == GuestArena::kEnd);
    CHECK(a1.alloc("mod:c", 1, 1, &e) == 0);
    a1.reset();
    CHECK(a1.next() == GuestArena::kPortEnd && a1.allocations().empty());
    CHECK(&b3native::guest_arena() == &b3native::guest_arena());
}

// ---- online_message.h: the port's words in the ONLINE dialog (string 105 / 107) ----
namespace {

constexpr gaddr kLookup = b3native::OnlineMessage::kLookup, kStrings = 0x00200000u;
gaddr string_at(std::uint32_t id) { return kStrings + 0x400u * (id & 0xFFu); }   // the fake table: one slot per id
std::uint32_t g_lookups = 0;
std::uint32_t fake_lookup(std::uint32_t /*table*/, std::uint32_t id) { ++g_lookups; return string_at(id); }
const NativeOverride kStrRows[] = {{kLookup, &traced_entry<kLookup, &native_entry<&fake_lookup>>, 4, &g_on, &g_nested}};
const TableEntry kStrEntries[] = {{kLookup, "GetString__10CB3StringsFi", "game:matched", 'i'}};

void put(gaddr at, const std::u16string& s) {
    for (std::size_t i = 0; i < s.size(); ++i) store<std::uint16_t>(at + 2 * static_cast<gaddr>(i), s[i]);
    store<std::uint16_t>(at + 2 * static_cast<gaddr>(s.size()), 0);
}
std::u16string get(gaddr at) {
    std::u16string s;
    for (std::uint16_t c; (c = load<std::uint16_t>(at + 2 * static_cast<gaddr>(s.size()))) != 0;) s += char16_t(c);
    return s;
}
gaddr look(HookTable& t, std::uint32_t id) {
    CallArgs c;
    c.gpr[0] = detail::gpr64(0x0051BA44u);   // the string table (D_0051BA44)
    c.gpr[1] = detail::gpr64(id);
    c.n_gpr = c.n_fpr = 8;
    c.sp = 0x01F00000u;
    CallResult r;
    t.top(kLookup)(c, r);
    return r.v0.w[0];
}
// The disc's string `id` from $B3_GAME_DIR/DATA/GLOBALUS.BIN (read at test time, never kept); empty without the data.
std::u16string disc_string(std::uint32_t id) {
    const char* dir = std::getenv("B3_GAME_DIR");
    if (!dir) return {};
    FILE* f = std::fopen((std::string(dir) + "/DATA/GLOBALUS.BIN").c_str(), "rb");
    if (!f) return {};
    std::vector<std::uint8_t> b;
    for (int c; (c = std::fgetc(f)) != EOF;) b.push_back(static_cast<std::uint8_t>(c));
    std::fclose(f);
    auto u32 = [&](std::size_t at) {
        return at + 4 <= b.size() ? std::uint32_t(b[at] | b[at + 1] << 8 | b[at + 2] << 16 | std::uint32_t(b[at + 3]) << 24) : 0u;
    };
    if (b.size() < 16 || id >= u32(8)) return {};
    std::u16string s;
    for (std::size_t at = u32(u32(12) + 4u * id); at + 2 <= b.size(); at += 2) {
        const char16_t c = char16_t(b[at] | b[at + 1] << 8);
        if (!c) break;
        s += c;
    }
    return s;
}

}  // namespace

TEST(online_message_swaps_only_the_disc_text_of_105_and_107) {
    using b3native::OnlineMessage;
    Published p;
    HookTable t;
    CHECK(t.attach_rows(kStrRows, kStrEntries, 1, false));
    ::unsetenv("B3NATIVE_ONLINE_MESSAGE");
    OnlineMessage m;
    CHECK(m.install(t));
    CHECK(t.chain(kLookup).size() == 2 && t.chain(kLookup)[1].owner == "port:online_message");
    // the FNV-1a 64 of the guest string (its UTF-16LE bytes) and its length
    put(string_at(1), u"AB");
    std::uint32_t n = 0;
    std::uint64_t h = 0xCBF29CE484222325ull;
    for (std::uint8_t c : {std::uint8_t('A'), std::uint8_t(0), std::uint8_t('B'), std::uint8_t(0)}) h = (h ^ c) * 0x100000001B3ull;
    CHECK(OnlineMessage::guest_hash(string_at(1), &n) == h && n == 2);
    CHECK(!OnlineMessage::is_retail(0) && !OnlineMessage::is_retail(string_at(1)));
    // another id, and 105 whose text is not the disc's (a text mod's): what the lower layer gave, the region unused
    put(string_at(0x68), u"Some other message");
    put(string_at(OnlineMessage::kNoAdapter), u"A text mod's own words");
    g_lookups = 0;
    CHECK(look(t, 0x68) == string_at(0x68));
    CHECK(look(t, OnlineMessage::kNoAdapter) == string_at(OnlineMessage::kNoAdapter));
    CHECK(g_lookups == 2);   // the lower layer ran each time
    CHECK(load<std::uint32_t>(OnlineMessage::kAt) == 0);
    // the disc's own text (when the game data is here): the port's words instead, for both ids
    const std::u16string d105 = disc_string(OnlineMessage::kNoAdapter), d107 = disc_string(OnlineMessage::kNoAdapter2);
    if (d105.empty()) {
        std::printf("  (no B3_GAME_DIR: the swap of the disc's text not checked)\n");
        return;
    }
    CHECK(d105.size() == OnlineMessage::kRetailUnits && d105 == d107);
    put(string_at(OnlineMessage::kNoAdapter), d105);
    put(string_at(OnlineMessage::kNoAdapter2), d107);
    CHECK(OnlineMessage::is_retail(string_at(OnlineMessage::kNoAdapter)));
    const std::u16string port(OnlineMessage::kText, OnlineMessage::kText + sizeof OnlineMessage::kText - 1);
    CHECK(look(t, OnlineMessage::kNoAdapter) == OnlineMessage::kAt && get(OnlineMessage::kAt) == port);
    CHECK(look(t, OnlineMessage::kNoAdapter2) == OnlineMessage::kAt && get(OnlineMessage::kAt) == port);
    CHECK(get(string_at(OnlineMessage::kNoAdapter)) == d105);   // the game's table untouched
    CHECK(look(t, 0x68) == string_at(0x68));
}

TEST(online_message_off_and_region_in_use_keep_the_game_text) {
    using b3native::OnlineMessage;
    Published p;
    {   // B3NATIVE_ONLINE_MESSAGE=0: not installed
        HookTable t;
        CHECK(t.attach_rows(kStrRows, kStrEntries, 1, false));
        ::setenv("B3NATIVE_ONLINE_MESSAGE", "0", 1);
        OnlineMessage m;
        CHECK(!m.install(t));
        CHECK(!t.hooked(kLookup));
        ::unsetenv("B3NATIVE_ONLINE_MESSAGE");
    }
    {   // no native lookup: not installed
        HookTable t;
        CHECK(t.attach_rows(kRows, kEntries, kN, false));
        OnlineMessage m;
        CHECK(!m.install(t));
    }
    const std::u16string d105 = disc_string(OnlineMessage::kNoAdapter);
    if (d105.empty()) return;
    // the region holds something: the game's text, and nothing written
    HookTable t;
    CHECK(t.attach_rows(kStrRows, kStrEntries, 1, false));
    OnlineMessage m;
    CHECK(m.install(t));
    put(string_at(OnlineMessage::kNoAdapter), d105);
    store<std::uint32_t>(OnlineMessage::kAt + 0x80u, 0x12345678u);
    CHECK(look(t, OnlineMessage::kNoAdapter) == string_at(OnlineMessage::kNoAdapter));
    CHECK(load<std::uint32_t>(OnlineMessage::kAt) == 0);
    CHECK(look(t, OnlineMessage::kNoAdapter) == string_at(OnlineMessage::kNoAdapter));   // stays off
}

// ---- speed_units.h: the units word (the game's language, 0x004E2918) at 2 around the display functions only ----
namespace {

using b3native::SpeedUnits;
// each fake display function answers the units word it saw; the records page also calls the records row (nested)
template <gaddr A>
int seen_word(int) { return static_cast<int>(load<std::uint32_t>(SpeedUnits::kLanguage)); }
int records_page(int x) { return seen_word<0>(x) * 100 + call_guest<int>(0x00376880u, 0); }
constexpr gaddr kSiteAddr[] = {0x001A2700u, 0x001A3B30u, 0x001A3C70u, 0x00364300u, 0x00376880u, 0x00376F60u,
                               0x003B8670u, 0x003CAAB0u, 0x003D8920u, 0x003F1490u, 0x0043A420u, 0x00500000u};
#define B3_UNITS_ROW(A, F) {A, &traced_entry<A, &native_entry<F>>, 4, &g_on, &g_nested}
const NativeOverride kUnitRows[] = {
    B3_UNITS_ROW(0x001A2700u, &seen_word<0x001A2700u>), B3_UNITS_ROW(0x001A3B30u, &seen_word<0x001A3B30u>),
    B3_UNITS_ROW(0x001A3C70u, &seen_word<0x001A3C70u>), B3_UNITS_ROW(0x00364300u, &seen_word<0x00364300u>),
    B3_UNITS_ROW(0x00376880u, &seen_word<0x00376880u>), B3_UNITS_ROW(0x00376F60u, &records_page),
    B3_UNITS_ROW(0x003B8670u, &seen_word<0x003B8670u>), B3_UNITS_ROW(0x003CAAB0u, &seen_word<0x003CAAB0u>),
    B3_UNITS_ROW(0x003D8920u, &seen_word<0x003D8920u>), B3_UNITS_ROW(0x003F1490u, &seen_word<0x003F1490u>),
    B3_UNITS_ROW(0x0043A420u, &seen_word<0x0043A420u>),
    B3_UNITS_ROW(0x00500000u, &seen_word<0x00500000u>),   // not a site: another reader of the language
};
#undef B3_UNITS_ROW
const TableEntry kUnitEntries[] = {
    {0x001A2700u, "UpdateSmallMessage__11CB3HUDScoreFf", "game:matched", 'i'}, {0x001A3B30u, "speedo_update", "game:matched", 'i'},
    {0x001A3C70u, "speedo_prepare", "game:matched", 'i'}, {0x00364300u, "item_value", "game:matched", 'i'},
    {0x00376880u, "records_row", "game:matched", 'i'}, {0x00376F60u, "records_page", "game:matched", 'i'},
    {0x003B8670u, "distance_text", "game:matched", 'i'}, {0x003CAAB0u, "score_list", "game:matched", 'i'},
    {0x003D8920u, "escape_distance", "game:matched", 'i'}, {0x003F1490u, "driver_stat", "game:matched", 'i'},
    {0x0043A420u, "stat", "game:matched", 'i'}, {0x00500000u, "other_reader", "game:matched", 'i'},
};
constexpr std::size_t kUnitN = sizeof kUnitRows / sizeof kUnitRows[0];
int g_units = 0;

}  // namespace

TEST(speed_units_sets_the_word_only_inside_the_display_functions) {
    static_assert(SpeedUnits::kSiteCount == 11, "the 11 display functions that read the units");
    for (const SpeedUnits::Site& s : SpeedUnits::kSites) {
        bool listed = false;
        for (gaddr a : kSiteAddr) listed = listed || a == s.address;
        CHECK(listed);
    }
    ::unsetenv("B3NATIVE_SPEED_UNITS");
    ::unsetenv("B3NATIVE_SPEED_LOG");
    Published p;
    HookTable t;
    CHECK(t.attach_rows(kUnitRows, kUnitEntries, kUnitN, true));
    SpeedUnits u;
    g_units = 0;
    CHECK(u.install(t, [] { return g_units; }));
    for (const SpeedUnits::Site& s : SpeedUnits::kSites) CHECK(t.chain(s.address).size() == 2 && t.chain(s.address)[1].owner == "port:speed_units");
    CHECK(!t.hooked(0x00500000u));
    store<std::uint32_t>(SpeedUnits::kLanguage, 1);   // the US game's language
    // MPH (the default): every function sees the game's word, nothing written
    for (gaddr a : kSiteAddr) CHECK(call_guest<int>(a, 0) == (a == 0x00376F60u ? 101 : 1));
    // KM/H: 2 inside each site (the records page and the row it calls: 2, no second swap), 1 again after, 1 elsewhere
    g_units = 1;
    for (gaddr a : kSiteAddr) {
        // the speedometer's Update still follows the MPH its Prepare latched above (it comes before Prepare here)
        const int want = a == 0x00500000u || a == SpeedUnits::kSpeedoUpdate ? 1 : 2;
        CHECK(call_guest<int>(a, 0) == (a == 0x00376F60u ? 202 : want));
        CHECK(load<std::uint32_t>(SpeedUnits::kLanguage) == 1);
    }
    store<std::uint32_t>(SpeedUnits::kLanguage, 0);   // US English 0: also miles, put back as 0
    CHECK(call_guest<int>(0x003B8670u, 0) == 2 && load<std::uint32_t>(SpeedUnits::kLanguage) == 0);
    store<std::uint32_t>(SpeedUnits::kLanguage, 5);   // a language that writes kilometres itself: untouched
    CHECK(call_guest<int>(0x003B8670u, 0) == 5 && load<std::uint32_t>(SpeedUnits::kLanguage) == 5);
    g_units = 0;
    CHECK(call_guest<int>(0x003B8670u, 0) == 5);
    store<std::uint32_t>(SpeedUnits::kLanguage, 1);
    // the speedometer: Update follows the setting its Prepare saw (a change in a race: from the next race)
    g_units = 1;
    CHECK(call_guest<int>(SpeedUnits::kSpeedoPrepare, 0) == 2);
    g_units = 0;
    CHECK(call_guest<int>(SpeedUnits::kSpeedoUpdate, 0) == 2);   // still km/h
    CHECK(call_guest<int>(0x003CAAB0u, 0) == 1);                  // the other sites: at once
    CHECK(call_guest<int>(SpeedUnits::kSpeedoPrepare, 0) == 1);  // the next race
    CHECK(call_guest<int>(SpeedUnits::kSpeedoUpdate, 0) == 1);
    g_units = 1;
    CHECK(call_guest<int>(SpeedUnits::kSpeedoUpdate, 0) == 1);
    CHECK(load<std::uint32_t>(SpeedUnits::kLanguage) == 1);
    // B3NATIVE_SPEED_UNITS (read at install) wins over the setting
    {
        HookTable t2;
        CHECK(t2.attach_rows(kUnitRows, kUnitEntries, kUnitN, true));
        ::setenv("B3NATIVE_SPEED_UNITS", "0", 1);
        SpeedUnits u2;
        CHECK(u2.install(t2, [] { return g_units; }) && u2.units() == 0);
        CHECK(call_guest<int>(0x003CAAB0u, 0) == 1);
        ::unsetenv("B3NATIVE_SPEED_UNITS");
    }
    store<std::uint32_t>(SpeedUnits::kLanguage, 0);
}

TEST(speed_units_all_sites_or_none) {
    ::unsetenv("B3NATIVE_SPEED_UNITS");
    Published p;
    HookTable t;   // one site missing: nothing installed (the game's units)
    CHECK(t.attach_rows(kUnitRows + 1, kUnitEntries + 1, kUnitN - 1, false));
    SpeedUnits u;
    CHECK(!u.install(t, [] { return 1; }));
    for (std::size_t i = 1; i < kUnitN; ++i) CHECK(!t.hooked(kUnitRows[i].address));
}

TEST_MAIN("test_hook_table: the hook table and the mod arena (mod kit 2a)")
