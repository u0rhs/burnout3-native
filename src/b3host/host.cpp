// b3host core: the out-of-line parts of b3host/host.h and the decomp B3Ptr.h bridge.
#include <cstdlib>
#include <cstdio>
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__APPLE__) || defined(__linux__)
#include <pthread.h>
#endif
#include "b3host/host.h"

#include <algorithm>
#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <vector>
#include <cstdio>

namespace b3host {

// boot2: B3HOST_FATAL_ABORT=1 turns a fatal error into abort() after printing it, so a crash handler (native_boot's)
// shows the host stack where it happened (there is no debugger on this host)
void diag(const char* tag, std::uint32_t a, std::uint32_t b, std::uint32_t c, std::uint32_t d, std::uint32_t e) {
    static const bool on = [] { const char* v = std::getenv("B3HOST_DIAG"); return v && *v && *v != '0'; }();
    if (on) std::fprintf(stderr, "diag %s %08X %u %u %u %u\n", tag, a, b, c, d, e);
}
void draft_fall_off(const char* site) {
    static std::mutex m;
    static std::vector<const char*> seen;
    const std::lock_guard<std::mutex> lock(m);
    if (std::find(seen.begin(), seen.end(), site) != seen.end()) return;
    seen.push_back(site);
    std::fprintf(stderr, "b3host: draft path reached (no retail value, returning a defined one; noted once): %s\n", site);
}
void fatal(const std::string& message) {
    static const bool abort_on = [] { const char* e = std::getenv("B3HOST_FATAL_ABORT"); return e && *e == '1'; }();
    const std::string full = g_fatal_context ? message + "; " + g_fatal_context() : message;
    if (abort_on) {
        std::fprintf(stderr, "fatal (B3HOST_FATAL_ABORT): %s\n", full.c_str());
        std::abort();
    }
    throw HostError(full);
}
void fatal_guest_pointer(gaddr address, std::uint32_t len) {
    char m[160];
    std::snprintf(m, sizeof m, "b3host: guest pointer outside RAM/scratchpad dereferenced: address %08X (%u bytes)",
                  static_cast<unsigned>(address), static_cast<unsigned>(len));
    fatal(m);
}

// mac3: the calling thread's own stack (host.h detail::StackRange): Linux pthread_getattr_np, macOS
// pthread_get_stackaddr_np (the top) / pthread_get_stacksize_np, Windows the TIB's StackBase / StackLimit (per fiber:
// read each time); elsewhere unknown (no clamp)
namespace detail {
StackRange thread_stack() {
#if defined(_WIN32)
    const auto* tib = reinterpret_cast<const NT_TIB*>(NtCurrentTeb());
    return {static_cast<const std::uint8_t*>(tib->StackLimit), static_cast<const std::uint8_t*>(tib->StackBase)};
#elif defined(__APPLE__) || defined(__linux__)
    thread_local StackRange r = {nullptr, nullptr};
    thread_local bool known = false;
    if (!known) {
        known = true;
#if defined(__APPLE__)
        const auto* top = static_cast<const std::uint8_t*>(pthread_get_stackaddr_np(pthread_self()));
        r = {top - pthread_get_stacksize_np(pthread_self()), top};
#else
        pthread_attr_t at;
        if (pthread_getattr_np(pthread_self(), &at) == 0) {
            void* lo = nullptr;
            std::size_t size = 0;
            if (pthread_attr_getstack(&at, &lo, &size) == 0)
                r = {static_cast<const std::uint8_t*>(lo), static_cast<const std::uint8_t*>(lo) + size};
            pthread_attr_destroy(&at);
        }
#endif
    }
    return r;
#else
    return {nullptr, nullptr};
#endif
}
}  // namespace detail

bool try_guest_address(const void* host, gaddr& out) {
    out = 0;
    if (!host) return true;
    if (detail::is_foreign(host)) { out = static_cast<gaddr>(reinterpret_cast<std::uintptr_t>(host)); return true; }   // boot2
    const Windows& w = detail::g_windows;
    const auto p = reinterpret_cast<std::uintptr_t>(host);
    if (w.ram) {
        const auto base = reinterpret_cast<std::uintptr_t>(w.ram);
        if (p >= base && p - base < w.ram_size) { out = static_cast<gaddr>(p - base); return true; }
    }
    if (w.scratchpad) {
        const auto base = reinterpret_cast<std::uintptr_t>(w.scratchpad);
        if (p >= base && p - base < kScratchpadSize) { out = kScratchpadBase + static_cast<gaddr>(p - base); return true; }
    }
    return false;
}

namespace detail {
namespace {
struct BounceTable {
    std::mutex m;
    std::unordered_map<const void*, BounceSite> sites;
};
BounceTable& bounce_table() {
    static BounceTable* t = new BounceTable;   // never destroyed: guest threads may still open windows at exit
    return *t;
}
}  // namespace
void note_bounce_open(std::uint32_t bytes, const void* site) {
    BounceTable& t = bounce_table();
    std::lock_guard<std::mutex> lock(t.m);
    BounceSite& s = t.sites[site];
    s.site = site;
    ++s.opens;
    if (bytes > s.max_bytes) s.max_bytes = bytes;
}
std::vector<BounceSite> bounce_sites() {
    BounceTable& t = bounce_table();
    std::lock_guard<std::mutex> lock(t.m);
    std::vector<BounceSite> v;
    for (const auto& kv : t.sites) v.push_back(kv.second);
    std::sort(v.begin(), v.end(), [](const BounceSite& a, const BounceSite& b) { return a.opens > b.opens; });
    return v;
}
}  // namespace detail

gaddr guest_address(const void* host) {
    gaddr g;
    if (!try_guest_address(host, g)) {
        char m[200];
        std::snprintf(m, sizeof m, "b3host::guest_address: host pointer %p is not in guest memory (RAM %p + %x, scratchpad %p)", host,
                      static_cast<const void*>(detail::g_windows.ram), detail::g_windows.ram_size,
                      static_cast<const void*>(detail::g_windows.scratchpad));
        fatal(m);
    }
    return g;
}

namespace {
const NativeOverride* g_native = nullptr;
std::size_t g_native_count = 0;
// rt1 (perf: the binary search was 19% of native_play's host time in the menus): a direct index over the code range,
// one word per 4-byte address -- the entry's position + 1, 0 for none -- so a lookup is one load; addresses outside
// the range (or unaligned) take the binary search as before. Same entries, same enabled check.
constexpr gaddr kIndexLo = 0x00100000u, kIndexHi = 0x00600000u;
std::vector<std::uint32_t> g_index;
}  // namespace

void set_native_overrides(const NativeOverride* table, std::size_t count) {
    g_native = table;
    g_native_count = count;
    g_index.assign((kIndexHi - kIndexLo) >> 2, 0u);
    for (std::size_t k = 0; k < count; ++k) {
        const gaddr a = table[k].address;
        if (a >= kIndexLo && a < kIndexHi && (a & 3u) == 0 && g_index[(a - kIndexLo) >> 2] == 0)
            g_index[(a - kIndexLo) >> 2] = static_cast<std::uint32_t>(k + 1);   // the first of equal addresses, as the search
    }
}
const NativeOverride* native_override(gaddr function) {
    std::size_t lo;
    if (function >= kIndexLo && function < kIndexHi && (function & 3u) == 0 && !g_index.empty()) {
        const std::uint32_t i = g_index[(function - kIndexLo) >> 2];
        if (i == 0) return nullptr;
        lo = i - 1;
    } else {
        // the table is sorted by address (the generator and the libm list are; set_native_overrides' contract)
        lo = 0;
        std::size_t hi = g_native_count;
        while (lo < hi) {
            const std::size_t mid = (lo + hi) / 2;
            if (g_native[mid].address < function) lo = mid + 1;
            else hi = mid;
        }
    }
    if (lo < g_native_count && g_native[lo].address == function && g_native[lo].enabled->load(std::memory_order_relaxed))
        return &g_native[lo];
    return nullptr;
}

void copy_to_guest(gaddr dst, const void* src, std::uint32_t len) {
    const auto* s = static_cast<const std::uint8_t*>(src);
    if (void* p = host_pointer(dst, len)) {
        std::memcpy(p, s, len);
        note_store(dst, len);
        return;
    }
    for (std::uint32_t i = 0; i < len; ++i) store<std::uint8_t>(dst + i, s[i]);
}

void copy_from_guest(void* dst, gaddr src, std::uint32_t len) {
    auto* d = static_cast<std::uint8_t*>(dst);
    if (const void* p = host_pointer(src, len)) {
        std::memcpy(d, p, len);
        return;
    }
    for (std::uint32_t i = 0; i < len; ++i) d[i] = load<std::uint8_t>(src + i);
}

void fill_guest(gaddr dst, std::uint8_t value, std::uint32_t len) {
    if (void* p = host_pointer(dst, len)) {
        std::memset(p, value, len);
        note_store(dst, len);
        return;
    }
    for (std::uint32_t i = 0; i < len; ++i) store<std::uint8_t>(dst + i, value);
}

std::uint64_t fnv1a64(const void* data, std::size_t len) {
    std::uint64_t h = 0xcbf29ce484222325ull;
    const auto* p = static_cast<const std::uint8_t*>(data);
    for (std::size_t i = 0; i < len; ++i) {
        h ^= p[i];
        h *= 0x100000001b3ull;
    }
    return h;
}

}  // namespace b3host

// The decomp's include/b3/B3Ptr.h (host branch) declares these two for B3GuestPtr<T>. Same rules as gp<T>.
void* B3GuestToHost(unsigned int guest) {
    if (guest == 0) return nullptr;
    void* p = b3host::host_pointer(guest, 1);
    if (!p) {
        char m[96];
        std::snprintf(m, sizeof m, "B3GuestToHost: guest pointer outside RAM/scratchpad: address %08X", guest);
        b3host::fatal(m);
    }
    return p;
}
unsigned int B3HostToGuest(const void* host) { return b3host::guest_address(host); }
