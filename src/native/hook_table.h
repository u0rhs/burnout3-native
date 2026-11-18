// hook_table.h -- the one hook table on the native table (mod kit Phase 2a, docs/modding/DESIGN.md §2.3 / §2.4,
// docs/modding/hooks.md).
//
// Every change to what a dispatched call at a retail guest address runs goes through here: the port's own wrappers
// (tools/native/pc_menu.h, card_silence.h, date_format.h, custom_soundtrack.h, now_playing_popup.h, menu_mouse.h, the
// debugging wrappers)
// and, from Phase 2b, the code mods. Per address the layers are fixed, bottom to top:
//
//   0  the table's own row: the generated function (or the libm / platform-tier row at that address)
//   1  kReplace  a mod's replacement (at most one per address)
//   2  kPort     the port's wrappers, in install order (the last installed is on top)
//   3  kWrap     the mods' wraps; the first in the load order (the first installed) is on top
//
// The top hook is the entry dispatch runs (b3host::native_override(addr)->entry); each hook reaches the layer below
// through its `next` (a plain entry the table fills in and rewrites when a layer is added beneath it). An address
// without hooks keeps its row's entry: dispatch is exactly what it was without the table (no extra call).
//
// A set of hooks is installed all or nothing, under one owner name ("port:pc_menu", later "mod:<id>"): every
// address must have a row, a mod layer may not hook the platform tier (unless the set allows it), a second
// replace on an address is a conflict naming both owners, and after seal() nothing more is accepted (the table is
// fixed for the run: determinism, and no locking on the hot path). On any refusal nothing changes.
//
// The hook caveat: a hook sees the calls that go through dispatch (call_guest, guest_fn macros, vcall, gfn, code that
// is not native). A direct C++ call between two generated units bypasses it; config/mod_hooks.txt names callees whose
// definitions decomp2host makes route such calls through dispatch while a hook is on them (b3host/hook_route.h).
#ifndef B3NATIVE_HOOK_TABLE_H
#define B3NATIVE_HOOK_TABLE_H

#include "native_table.h"

#include "b3host/host.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace b3native {

using HookEntry = void (*)(const b3host::CallArgs&, b3host::CallResult&);

enum class HookLayer : std::uint8_t { kGenerated = 0, kReplace = 1, kPort = 2, kWrap = 3 };
const char* layer_name(HookLayer l);

struct Hook {
    b3host::gaddr address = 0;
    HookLayer layer = HookLayer::kPort;
    HookEntry fn = nullptr;      // what dispatch runs at this layer
    HookEntry* next = nullptr;   // filled with the entry of the layer below (null: the hook never calls it)
};

// A hook with its own data (the form Phase 2b's C ABI needs: `replace(self, addr, fn, user)`): fn(args, result, user,
// next). bind() gives it one of a fixed pool of trampolines (kBoundSlots in the process, never reused); a Hook with a
// null fn when the pool is used up (install then refuses it).
using HookUserFn = void (*)(const b3host::CallArgs&, b3host::CallResult&, void* user, HookEntry next);
constexpr unsigned kBoundSlots = 1024;
Hook bind_hook(b3host::gaddr address, HookLayer layer, HookUserFn fn, void* user);
unsigned bound_slots_used();

class HookTable {
public:
    HookTable() = default;
    HookTable(const HookTable&) = delete;
    HookTable& operator=(const HookTable&) = delete;
    ~HookTable();   // a published table's route bits are withdrawn (the dispatch table itself stays installed)
    // The table's rows as they are installed now (b3native::install's entries, after any switch-offs; `entries` in
    // address order). Copies them; publish = true installs the copy as the dispatch table (set_native_overrides),
    // false keeps it private (tests). Returns false if the installed table does not match `entries`.
    bool attach(const TableEntry* entries, std::size_t count, bool publish = true) {
        std::size_t n = 0;
        const b3host::NativeOverride* rows = installed_rows(n);   // native_table.cpp
        return rows != nullptr && n == count && attach_rows(rows, entries, count, publish);
    }
    // The same from explicit rows (tests; `rows` and `entries` in the same address order).
    bool attach_rows(const b3host::NativeOverride* rows, const TableEntry* entries, std::size_t count, bool publish);
    bool attached() const { return !rows_.empty(); }

    // Installs `n` hooks for `owner`, all or nothing. allow_platform: a mod layer may hook a platform-tier row
    // ([code] platform = yes with the user's "Developer mods"). On a refusal *error says why (one line) and nothing
    // changes.
    bool install(const std::string& owner, const Hook* hooks, std::size_t n, std::string* error = nullptr,
                 bool allow_platform = false);
    bool install(const std::string& owner, const std::vector<Hook>& hooks, std::string* error = nullptr,
                 bool allow_platform = false) {
        return install(owner, hooks.data(), hooks.size(), error, allow_platform);
    }
    // Switches rows off for `owner` (native_override() then returns null for them: the fallback runs retail;
    // tools/native/explore_standins.h RetailFor), all or nothing like install.
    bool switch_off(const std::string& owner, const std::vector<b3host::gaddr>& addresses, std::atomic<bool>* off,
                    std::string* error = nullptr);
    // No more installs (native_play: after the port's features and the mods are in).
    void seal() { sealed_ = true; }
    bool sealed() const { return sealed_; }

    // ---- inspection ----
    bool has_row(b3host::gaddr a) const { return find(a) >= 0; }
    HookEntry generated(b3host::gaddr a) const;   // the row's own entry (layer 0), null: no row
    HookEntry top(b3host::gaddr a) const;         // what dispatch runs now, null: no row
    bool hooked(b3host::gaddr a) const;           // any layer above the row
    struct Link {
        HookLayer layer;
        std::string owner;
        HookEntry fn;
    };
    std::vector<Link> chain(b3host::gaddr a) const;   // bottom to top, the row first ("generated", its source)
    std::vector<b3host::gaddr> hooked_addresses() const;
    const std::vector<std::string>& owners() const { return owners_; }
    const b3host::NativeOverride* rows() const { return rows_.data(); }
    std::size_t size() const { return rows_.size(); }

private:
    struct Placed {
        Hook hook;
        std::uint32_t owner;   // index into owners_
        std::uint32_t seq;     // install order over the whole table
    };
    long find(b3host::gaddr a) const;   // row index, -1: none
    void relink(std::size_t row);
    std::vector<std::uint64_t>& route_bits();   // b3host::g_route_bits (b3host/hook_route.h) of a published table
    std::vector<b3host::NativeOverride> rows_;
    std::vector<b3host::gaddr> addr_;          // rows_' addresses (sorted)
    std::vector<const char*> name_, source_;   // per row, from the entries
    std::vector<HookEntry> base_;              // per row: the entry before any hook
    std::vector<std::vector<Placed>> placed_;  // per row, any order (relink sorts)
    std::vector<std::string> owners_;
    std::vector<std::uint64_t> route_bits_;
    std::uint32_t seq_ = 0;
    bool published_ = false, sealed_ = false;
};

// The process's table (native_play): attached once to the installed native table.
HookTable& hook_table();

// ---- the mod arena: free guest RAM below the ELF (DESIGN.md §2.2 memory) ----
// One allocator for the guest RAM the native game never uses: 0xC0000 up to the ELF's first byte (0x100000, its
// PT_LOAD). The first 32 KB are the port's own fixed regions (the addresses they always had; a new port region goes
// in that block, never moving another), the rest is handed to mods in load order and call order, so an address is the
// same in every run with the same set. The users check their region reads zero before the first write (the game
// never writes there [C every race_check and golden run so far], but a region in use leaves the feature off).
struct ArenaRegion {
    b3host::gaddr address;
    std::uint32_t size;
    const char* what;
};
class GuestArena {
public:
    static constexpr b3host::gaddr kBase = 0x000C0000u, kPortEnd = 0x000C8000u, kEnd = 0x00100000u;
    // the port's regions (constexpr: the features' addresses are compile-time constants)
    static constexpr ArenaRegion kPcMenu{0x000C0000u, 0x400u, "port:pc_menu (menu rows, help texts, labels)"};
    static constexpr ArenaRegion kDateFormat{0x000C0800u, 0x20u, "port:date_format (the day-first date string)"};
    static constexpr ArenaRegion kCustomSoundtrack{0x000C0C00u, 0x300u, "port:custom_soundtrack (now-playing texts)"};
    static constexpr ArenaRegion kNowPlaying{0x000C0F00u, 0x300u, "port:now_playing (another music app's popup texts)"};
    static constexpr ArenaRegion kOnlineMessage{0x000C1200u, 0x100u, "port:online_message (the ONLINE dialog's text)"};
    static constexpr ArenaRegion kEaTraxSongs{0x000C1400u, 0x1800u, "port:eatrax_songs (the EA TRAX song table with the custom songs)"};
    static constexpr ArenaRegion kEaTraxText{0x000C2C00u, 0x800u, "port:eatrax_text (the custom songs' titles, artists, albums)"};
    static constexpr ArenaRegion kPort[] = {kPcMenu, kDateFormat, kCustomSoundtrack, kNowPlaying, kOnlineMessage, kEaTraxSongs, kEaTraxText};

    // `bytes` for `owner` at a multiple of `align` (a power of two, 1..4096), zero-length refused; 0 when it does not
    // fit (error says why). Deterministic: the n-th call gets the same address in every run with the same calls.
    b3host::gaddr alloc(const std::string& owner, std::uint32_t bytes, std::uint32_t align = 16, std::string* error = nullptr);
    struct Alloc {
        b3host::gaddr address;
        std::uint32_t size;
        std::string owner;
    };
    const std::vector<Alloc>& allocations() const { return allocs_; }
    b3host::gaddr next() const { return next_; }
    void reset() { allocs_.clear(); next_ = kPortEnd; }
    // sdk2b: back to the first `count` allocations (a code mod that did not load gives its memory back; its allocations
    // are the last ones, so the next mod gets the addresses it would have had without it)
    void truncate(std::size_t count) {
        if (count >= allocs_.size()) return;
        allocs_.resize(count);
        next_ = allocs_.empty() ? kPortEnd : allocs_.back().address + allocs_.back().size;
    }

private:
    std::vector<Alloc> allocs_;
    b3host::gaddr next_ = kPortEnd;
};
GuestArena& guest_arena();

// compile-time checks of the fixed regions: inside the port block, in order, no overlap
constexpr bool arena_port_regions_ok() {
    b3host::gaddr at = GuestArena::kBase;
    for (const ArenaRegion& r : GuestArena::kPort) {
        if (r.address < at || r.size == 0 || r.address + r.size > GuestArena::kPortEnd) return false;
        at = r.address + r.size;
    }
    return true;
}
static_assert(arena_port_regions_ok(), "the port's arena regions overlap or leave the port block");

}  // namespace b3native

#endif
