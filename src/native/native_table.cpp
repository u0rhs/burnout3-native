// The native backend's table (src/native/native_table.h).
#include "native_table.h"

#include "b3host/call.h"
#include "b3host/native_rows.h"
#if __has_include("b3port/native_rows.h")
#include "b3port/native_rows.h"   // gen1: the units' own rows (DECOMP2HOST_NATIVE_ROWS=1)
#else
#include "b3port/game_overrides.h"
#endif
#include "b3port/libm_overrides.h"
#include "functions.h"   // build/native (tools/native/gen_table.py)
#include "platform.h"

#include <memory>

#include <algorithm>
#include <cstdlib>
#include <atomic>
#include <vector>

#ifdef B3PORT_NATIVE_ROWS
// gen1: the generated units' row arrays (global, defined at the end of each unit)
#define B3N_DECL(sym, n) extern const b3host::NativeRow sym[n];
B3PORT_NATIVE_ROWS(B3N_DECL)
#undef B3N_DECL
#endif

namespace b3native {
namespace {

constexpr b3host::FunctionInfo kInfo[] = {
#define B3N_INFO(addr, size, kind, c, name) {addr, size, kind, c, name},
    B3NATIVE_FUNCTIONS(B3N_INFO)
#undef B3N_INFO
};

template <auto Fn> constexpr char ret_of() { return b3host::native_ret_of<Fn>(); }

using Raw = b3host::NativeRow;
// Order = precedence on a duplicate address: generated game code, then libm, then the platform tier.
struct Part {
    const Raw* rows;
    std::size_t n;
};
#ifdef B3PORT_NATIVE_ROWS
// gen1: the game rows are the units' own arrays (each compiled with its unit), in B3PORT_GAME_OVERRIDES order
const Part kGame[] = {
#define B3N_PART(sym, n) {sym, n},
    B3PORT_NATIVE_ROWS(B3N_PART)
#undef B3N_PART
    {nullptr, 0},
};
#define B3N_COUNT(sym, n) +(n)
constexpr std::size_t kNGame = 0 B3PORT_NATIVE_ROWS(B3N_COUNT);
#undef B3N_COUNT
#else
#define B3N_GAME(nm, addr, bytes, fnv, fn, track) B3_NATIVE_ROW(nm, addr, bytes, fn, track),
const Raw kGameRows[] = {
    B3PORT_GAME_OVERRIDES(B3N_GAME)
    {0, nullptr, 0, nullptr, nullptr, 0},
};
#undef B3N_GAME
const Part kGame[] = {{kGameRows, sizeof kGameRows / sizeof kGameRows[0] - 1}, {nullptr, 0}};
constexpr std::size_t kNGame = sizeof kGameRows / sizeof kGameRows[0] - 1;
#endif
// movie3: rows ahead of the game's (the native movie libraries replace generated ones at the same addresses)
const Raw kFirst[] = {
#define B3N_MOVIE(nm, addr, fn) {addr, &b3host::traced_entry<addr, &fn>, 0u, nm, "platform", 'n'},
    B3NATIVE_MOVIE(B3N_MOVIE)
#undef B3N_MOVIE
};
// sif-lane: the libraries whose IOP side the platform answers at their entry points (platform.h B3NATIVE_FIRST)
const Raw kFirstDev[] = {
#define B3N_FIRST(nm, addr, fn, ret) {addr, &b3host::traced_entry<addr, &fn>, 0u, nm, "platform", ret},
    B3NATIVE_FIRST(B3N_FIRST)
#undef B3N_FIRST
};
const Raw kTail[] = {
#define B3N_LIBM(nm, addr, bytes, fnv, ceiling, fn) \
    {addr, &b3host::traced_entry<addr, &b3host::native_entry<&fn>>, 0u, nm, "libm", ret_of<&fn>()},
    B3PORT_LIBM_OVERRIDES(B3N_LIBM)
#undef B3N_LIBM
#define B3N_PLAT(nm, addr, fn) {addr, &b3host::traced_entry<addr, &fn>, 0u, nm, "platform", 'n'},
    B3NATIVE_PLATFORM(B3N_PLAT)
#undef B3N_PLAT
};
// kRaw[i]: the game rows (part by part), then kTail -- the one array the table was before
std::vector<Raw> g_raw;
const std::vector<Raw>& raw() {
    if (g_raw.empty()) {
        const char* ipu = std::getenv("B3NATIVE_MOVIE_IPU");
        if (!(ipu && *ipu && *ipu != '0'))
            for (const Raw& r : kFirst) g_raw.push_back(r);
        for (const Raw& r : kFirstDev) g_raw.push_back(r);
        for (const Part& p : kGame)
            for (std::size_t k = 0; k < p.n; ++k) g_raw.push_back(p.rows[k]);
        for (const Raw& r : kTail) g_raw.push_back(r);
    }
    return g_raw;
}
constexpr std::size_t kN = sizeof kFirst / sizeof kFirst[0] + sizeof kFirstDev / sizeof kFirstDev[0] + kNGame + sizeof kTail / sizeof kTail[0];

std::atomic<bool> g_on{true};
// native3 (S2, --retail): one enable flag per entry, so the platform's entries stay on while the rest are switched off
std::unique_ptr<std::atomic<bool>[]> g_on_each;
std::vector<std::atomic<std::uint64_t>> g_nested(kN);
std::vector<b3host::NativeOverride> g_table;
std::vector<TableEntry> g_entries;
std::vector<std::size_t> g_entry_raw;   // g_entries[k] came from kRaw[g_entry_raw[k]]

}  // namespace

const b3host::FunctionInfo* functions(std::size_t& count) {
    count = sizeof kInfo / sizeof kInfo[0];
    return kInfo;
}

const TableEntry* install(std::size_t& count) {
    if (g_table.empty()) {
        const std::vector<Raw>& kRaw = raw();
        std::vector<std::size_t> order(kRaw.size());   // kN less kFirst under B3NATIVE_MOVIE_IPU
        for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
        std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) { return kRaw[a].address < kRaw[b].address; });
        for (std::size_t i : order) {
            if (!g_table.empty() && g_table.back().address == kRaw[i].address) continue;   // first in kRaw wins
            // boot2: a platform-tier entry costs what the retail function it stands for would retire statically,
            // its size / 4 from the decomp's function list (as generated game code: bytes / 4), so a loop that spins
            // on a platform call (RW Audio's `while (!reply) FlushCache(0);`) advances virtual time and the IOP's
            // reply comes; addresses with no listed size cost 0 [L: the static model, not a fitted cost]
            std::uint32_t cost = kRaw[i].cost;
            if (cost == 0 && kRaw[i].source[0] == 'p') {
                const b3host::FunctionInfo* f = std::lower_bound(std::begin(kInfo), std::end(kInfo), kRaw[i].address,
                    [](const b3host::FunctionInfo& x, b3host::gaddr a) { return x.address < a; });
                if (f != std::end(kInfo) && f->address == kRaw[i].address) cost = f->size / 4u;
            }
            if (!g_on_each) {
                g_on_each = std::make_unique<std::atomic<bool>[]>(kN);
                for (std::size_t k = 0; k < kN; ++k) g_on_each[k].store(true);
            }
            g_table.push_back({kRaw[i].address, kRaw[i].entry, cost, &g_on_each[i], &g_nested[i]});
            g_entries.push_back({kRaw[i].address, kRaw[i].name, kRaw[i].source, kRaw[i].ret});
            g_entry_raw.push_back(i);
        }
    }
    b3host::set_native_overrides(g_table.data(), g_table.size());
    std::size_t n;
    const b3host::FunctionInfo* info = functions(n);
    b3host::set_function_info(info, n);
    count = g_entries.size();
    return g_entries.data();
}

const b3host::NativeOverride* installed_rows(std::size_t& count) {
    count = g_table.size();
    return g_table.empty() ? nullptr : g_table.data();
}

std::uint64_t entered(std::size_t index) {
    return index < g_entry_raw.size() ? g_nested[g_entry_raw[index]].load(std::memory_order_relaxed) : 0;
}
void reset_entered() {
    for (auto& c : g_nested) c.store(0, std::memory_order_relaxed);
}

const TableEntry* entry_at(b3host::gaddr address) {
    auto it = std::lower_bound(g_entries.begin(), g_entries.end(), address,
                               [](const TableEntry& e, b3host::gaddr a) { return e.address < a; });
    return it != g_entries.end() && it->address == address ? &*it : nullptr;
}

}  // namespace b3native
