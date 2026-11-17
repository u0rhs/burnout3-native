// The hook table and the mod arena (src/native/hook_table.h).
#include "hook_table.h"

#include "b3host/hook_route.h"

#include <algorithm>
#include <cstdio>
#include <utility>

namespace b3native {

const char* layer_name(HookLayer l) {
    switch (l) {
    case HookLayer::kGenerated: return "generated";
    case HookLayer::kReplace: return "replace";
    case HookLayer::kPort: return "port";
    case HookLayer::kWrap: return "wrap";
    }
    return "?";
}

// ---- bound hooks: a fixed pool of trampolines, each with its function, user pointer and next ----
namespace {
struct Slot {
    HookUserFn fn;
    void* user;
    HookEntry next;
};
Slot g_slots[kBoundSlots];
unsigned g_slots_used = 0;
template <unsigned K>
void bound(const b3host::CallArgs& a, b3host::CallResult& r) {
    const Slot& s = g_slots[K];
    s.fn(a, r, s.user, s.next);
}
template <unsigned... K>
constexpr std::array<HookEntry, sizeof...(K)> make_bound(std::integer_sequence<unsigned, K...>) {
    return {{&bound<K>...}};
}
constexpr auto kBound = make_bound(std::make_integer_sequence<unsigned, kBoundSlots>{});

std::string hex(b3host::gaddr a) {
    char b[16];
    std::snprintf(b, sizeof b, "0x%08X", a);
    return b;
}
}  // namespace

Hook bind_hook(b3host::gaddr address, HookLayer layer, HookUserFn fn, void* user) {
    Hook h;
    h.address = address;
    h.layer = layer;
    if (fn == nullptr || g_slots_used >= kBoundSlots) return h;   // fn null: install refuses it
    Slot& s = g_slots[g_slots_used];
    s.fn = fn;
    s.user = user;
    s.next = nullptr;
    h.fn = kBound[g_slots_used];
    h.next = &s.next;
    ++g_slots_used;
    return h;
}
unsigned bound_slots_used() { return g_slots_used; }

// ---- the table ----
bool HookTable::attach_rows(const b3host::NativeOverride* rows, const TableEntry* entries, std::size_t count, bool publish) {
    if (attached() || count == 0 || rows == nullptr || entries == nullptr) return false;
    for (std::size_t k = 0; k < count; ++k)
        if (rows[k].address != entries[k].address || (k && entries[k].address <= entries[k - 1].address)) return false;
    rows_.assign(rows, rows + count);
    addr_.resize(count);
    name_.resize(count);
    source_.resize(count);
    base_.resize(count);
    placed_.assign(count, {});
    for (std::size_t k = 0; k < count; ++k) {
        addr_[k] = rows[k].address;
        name_[k] = entries[k].name;
        source_[k] = entries[k].source;
        base_[k] = rows[k].entry;
    }
    if (publish) {
        b3host::set_native_overrides(rows_.data(), rows_.size());
        b3host::g_route_bits = nullptr;   // no hooks yet
        published_ = true;
    }
    return true;
}

HookTable::~HookTable() {
    if (!route_bits_.empty() && b3host::g_route_bits == route_bits_.data()) b3host::g_route_bits = nullptr;
}

long HookTable::find(b3host::gaddr a) const {
    auto it = std::lower_bound(addr_.begin(), addr_.end(), a);
    return it != addr_.end() && *it == a ? static_cast<long>(it - addr_.begin()) : -1;
}

HookEntry HookTable::generated(b3host::gaddr a) const {
    const long i = find(a);
    return i < 0 ? nullptr : base_[static_cast<std::size_t>(i)];
}
HookEntry HookTable::top(b3host::gaddr a) const {
    const long i = find(a);
    return i < 0 ? nullptr : rows_[static_cast<std::size_t>(i)].entry;
}
bool HookTable::hooked(b3host::gaddr a) const {
    const long i = find(a);
    return i >= 0 && !placed_[static_cast<std::size_t>(i)].empty();
}

std::vector<HookTable::Link> HookTable::chain(b3host::gaddr a) const {
    std::vector<Link> out;
    const long i = find(a);
    if (i < 0) return out;
    const std::size_t row = static_cast<std::size_t>(i);
    out.push_back({HookLayer::kGenerated, source_[row] ? source_[row] : "", base_[row]});
    std::vector<Placed> v = placed_[row];
    std::stable_sort(v.begin(), v.end(), [](const Placed& x, const Placed& y) {
        if (x.hook.layer != y.hook.layer) return x.hook.layer < y.hook.layer;
        return x.hook.layer == HookLayer::kWrap ? x.seq > y.seq : x.seq < y.seq;
    });
    for (const Placed& p : v) out.push_back({p.hook.layer, owners_[p.owner], p.hook.fn});
    return out;
}

std::vector<b3host::gaddr> HookTable::hooked_addresses() const {
    std::vector<b3host::gaddr> out;
    for (std::size_t k = 0; k < placed_.size(); ++k)
        if (!placed_[k].empty()) out.push_back(addr_[k]);
    return out;
}

// Bottom to top: replace, port (install order), wraps (the first installed on top). Each hook's next is the entry
// below it; the top one is the row's entry.
void HookTable::relink(std::size_t row) {
    std::vector<Placed>& v = placed_[row];
    std::stable_sort(v.begin(), v.end(), [](const Placed& x, const Placed& y) {
        if (x.hook.layer != y.hook.layer) return x.hook.layer < y.hook.layer;
        return x.hook.layer == HookLayer::kWrap ? x.seq > y.seq : x.seq < y.seq;
    });
    HookEntry below = base_[row];
    for (Placed& p : v) {
        if (p.hook.next) *p.hook.next = below;
        below = p.hook.fn;
    }
    rows_[row].entry = below;
}

bool HookTable::install(const std::string& owner, const Hook* hooks, std::size_t n, std::string* error, bool allow_platform) {
    auto fail = [&](const std::string& why) {
        if (error) *error = why;
        return false;
    };
    if (!attached()) return fail(owner + ": the hook table is not attached to the native table");
    if (sealed_) return fail(owner + ": the hook table is sealed (hooks go in before the game starts)");
    if (owner.empty()) return fail("a hook set needs an owner name");
    if (std::find(owners_.begin(), owners_.end(), owner) != owners_.end()) return fail(owner + ": already installed");
    if (n == 0 || hooks == nullptr) return fail(owner + ": no hooks");
    for (std::size_t k = 0; k < n; ++k) {
        const Hook& h = hooks[k];
        const long i = find(h.address);
        if (h.fn == nullptr) return fail(owner + ": hook " + std::to_string(k) + " at " + hex(h.address) + " has no function (no trampoline left?)");
        if (h.layer == HookLayer::kGenerated || static_cast<unsigned>(h.layer) > static_cast<unsigned>(HookLayer::kWrap))
            return fail(owner + ": " + hex(h.address) + ": not a hook layer");
        if (i < 0) return fail(owner + ": " + hex(h.address) + " has no native table entry (not native, or inlined in the decomp)");
        const std::size_t row = static_cast<std::size_t>(i);
        const bool mod_layer = h.layer == HookLayer::kReplace || h.layer == HookLayer::kWrap;
        if (mod_layer && !allow_platform && source_[row] && std::string(source_[row]) == "platform")
            return fail(owner + ": " + hex(h.address) + " " + name_[row] + " is the platform tier (needs [code] platform = yes and Developer mods)");
        if (h.layer == HookLayer::kReplace) {
            for (const Placed& p : placed_[row])
                if (p.hook.layer == HookLayer::kReplace)
                    return fail(owner + " conflicts with " + owners_[p.owner] + ": both replace " + hex(h.address) + " " + name_[row]);
            for (std::size_t j = 0; j < k; ++j)
                if (hooks[j].layer == HookLayer::kReplace && hooks[j].address == h.address)
                    return fail(owner + ": replaces " + hex(h.address) + " " + name_[row] + " twice");
        }
    }
    const std::uint32_t oi = static_cast<std::uint32_t>(owners_.size());
    owners_.push_back(owner);
    std::vector<std::size_t> touched;
    for (std::size_t k = 0; k < n; ++k) {
        const std::size_t row = static_cast<std::size_t>(find(hooks[k].address));
        placed_[row].push_back({hooks[k], oi, seq_++});
        touched.push_back(row);
    }
    std::sort(touched.begin(), touched.end());
    touched.erase(std::unique(touched.begin(), touched.end()), touched.end());
    for (std::size_t row : touched) relink(row);
    if (published_) {
        // dispatch reads rows_ in place (set_native_overrides kept the pointer): only the route bits change
        auto& bits = route_bits();
        for (std::size_t row : touched) {
            const b3host::gaddr a = addr_[row];
            if (a >= b3host::kRouteLo && a < b3host::kRouteHi && (a & 3u) == 0) {
                const std::uint32_t w = (a - b3host::kRouteLo) >> 2;
                bits[w >> 6] |= std::uint64_t(1) << (w & 63);
            }
        }
        b3host::g_route_bits = bits.data();
    }
    return true;
}

bool HookTable::switch_off(const std::string& owner, const std::vector<b3host::gaddr>& addresses, std::atomic<bool>* off,
                           std::string* error) {
    auto fail = [&](const std::string& why) {
        if (error) *error = why;
        return false;
    };
    if (!attached()) return fail(owner + ": the hook table is not attached to the native table");
    if (sealed_) return fail(owner + ": the hook table is sealed");
    if (off == nullptr || addresses.empty()) return fail(owner + ": nothing to switch off");
    if (std::find(owners_.begin(), owners_.end(), owner) != owners_.end()) return fail(owner + ": already installed");
    for (b3host::gaddr a : addresses)
        if (find(a) < 0) return fail(owner + ": " + hex(a) + " has no native entry");
    owners_.push_back(owner);
    for (b3host::gaddr a : addresses) rows_[static_cast<std::size_t>(find(a))].enabled = off;
    return true;
}

std::vector<std::uint64_t>& HookTable::route_bits() {
    if (route_bits_.empty()) route_bits_.assign(((b3host::kRouteHi - b3host::kRouteLo) >> 2) / 64 + 1, 0);
    return route_bits_;
}

HookTable& hook_table() {
    static HookTable t;
    return t;
}

// ---- the arena ----
b3host::gaddr GuestArena::alloc(const std::string& owner, std::uint32_t bytes, std::uint32_t align, std::string* error) {
    auto fail = [&](const std::string& why) -> b3host::gaddr {
        if (error) *error = why;
        return 0;
    };
    if (owner.empty()) return fail("arena: an allocation needs an owner");
    if (bytes == 0) return fail(owner + ": arena: zero bytes");
    if (align == 0 || align > 4096 || (align & (align - 1)) != 0) return fail(owner + ": arena: alignment " + std::to_string(align) + " (a power of two, 1..4096)");
    const std::uint64_t at = (std::uint64_t(next_) + align - 1) & ~std::uint64_t(align - 1);
    if (at + bytes > kEnd)
        return fail(owner + ": arena: " + std::to_string(bytes) + " bytes do not fit (" + std::to_string(kEnd > at ? kEnd - at : 0) +
                    " left below the ELF)");
    allocs_.push_back({static_cast<b3host::gaddr>(at), bytes, owner});
    next_ = static_cast<b3host::gaddr>(at + bytes);
    return static_cast<b3host::gaddr>(at);
}

GuestArena& guest_arena() {
    static GuestArena a;
    return a;
}

}  // namespace b3native
