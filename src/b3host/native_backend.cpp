// b3host native backend (S0): see include/b3host/native_backend.h and docs/standalone.md §8.
#include "b3host/native_backend.h"
#include "b3host/watch.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <mutex>
#include <new>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/mman.h>
#include <sys/resource.h>
#include <unistd.h>
#endif

namespace b3host {

namespace {
const FunctionInfo* g_info = nullptr;
std::size_t g_info_count = 0;

std::string hex32(std::uint32_t v) {
    char b[16];
    std::snprintf(b, sizeof b, "%08X", v);
    return b;
}

std::string args_text(const CallArgs& a) {
    std::string s = "a0-a3 ";
    for (unsigned i = 0; i < 4; ++i) s += hex32(a.gpr[i].w[0]) + (i < 3 ? " " : "");
    s += " f12 " + hex32(a.fpr[0]) + " sp " + hex32(a.sp);
    return s;
}
}  // namespace

void set_function_info(const FunctionInfo* table, std::size_t count) {
    g_info = table;
    g_info_count = count;
}

const FunctionInfo* function_containing(gaddr address) {
    std::size_t lo = 0, hi = g_info_count;   // first entry > address
    while (lo < hi) {
        const std::size_t mid = (lo + hi) / 2;
        if (g_info[mid].address <= address) lo = mid + 1;
        else hi = mid;
    }
    if (lo == 0) return nullptr;
    const FunctionInfo* f = &g_info[lo - 1];
    return address < f->address + f->size ? f : nullptr;
}

const FunctionInfo* function_at(gaddr entry) {
    const FunctionInfo* f = function_containing(entry);
    return f && f->address == entry ? f : nullptr;
}

std::string describe_function(gaddr address) {
    if (const FunctionInfo* f = function_at(address))
        return std::string(f->name) + " [" + f->kind + ", " + f->c + "]";
    if (const FunctionInfo* f = function_containing(address))
        return std::string("inside ") + f->name + "+0x" + hex32(address - f->address) + " [" + f->kind + "]";
    return "unknown";
}

namespace native_chain {
std::vector<gaddr> snapshot() {
    const unsigned n = t_depth < kMax ? t_depth : kMax;
    return std::vector<gaddr>(t_chain, t_chain + n);
}
std::string format(const std::vector<gaddr>& chain) {
    std::string s;
    for (gaddr a : chain) {
        if (!s.empty()) s += " > ";
        const FunctionInfo* f = function_at(a);
        s += hex32(a);
        if (f) s += std::string(" ") + f->name;
    }
    return s.empty() ? "(top)" : s;
}
}  // namespace native_chain

// mac4: fatal()'s context: the native chain at the error (gone by the time a HostError is caught) and the last foreign
// guest pointer this thread formed
namespace {
std::string fatal_context() {
    std::string s = "native chain: " + native_chain::format(native_chain::snapshot());
    if (detail::t_any_foreign) s += "; last foreign guest pointer formed: " + hex32(detail::t_last_foreign);
    return s;
}
const bool g_fatal_context_set = [] { if (!g_fatal_context) g_fatal_context = fatal_context; return true; }();
}  // namespace

MissingNative::MissingNative(gaddr a, const CallArgs& args_)
    : HostError("no native function at " + hex32(a) + " " + describe_function(a) + "; " + args_text(args_) +
                "; chain: " + native_chain::format(native_chain::snapshot())),
      address(a), args(args_), chain(native_chain::snapshot()) {}

NativeFault::NativeFault(gaddr a, std::uint32_t n, bool w)
    : HostError(std::string("native ") + (w ? "write" : "read") + " of " + std::to_string(n) + " bytes at " + hex32(a) +
                " (outside RAM/scratchpad: MMIO, VU memory or past RAM); chain: " +
                native_chain::format(native_chain::snapshot())),
      address(a), len(n), write(w), chain(native_chain::snapshot()) {}

// ---- hostfix: guarded regions ----
namespace {
// The live regions' guards, for the crash report. Written under g_guard_mutex (construction / destruction), read
// without locks by guard_overrun (a signal handler): `data` is published last on register and cleared first on release.
struct GuardSlot {
    std::atomic<std::uintptr_t> data{0};
    std::uintptr_t lo = 0, end = 0, hi = 0;   // reservation start, data + size, reservation end
    const char* name = nullptr;
};
constexpr int kGuardSlots = 32;
GuardSlot g_guard_slots[kGuardSlots];
std::mutex g_guard_mutex;

std::size_t page_size() {
#ifdef _WIN32
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return si.dwPageSize;
#else
    return static_cast<std::size_t>(sysconf(_SC_PAGESIZE));
#endif
}
std::size_t round_up(std::size_t v, std::size_t to) { return (v + to - 1) / to * to; }
}  // namespace

namespace {
// Reserves guard + committed + guard bytes, the middle read-write; null when the address space is not there
std::uint8_t* reserve_guarded(std::size_t guard, std::size_t committed) {
    const std::size_t total = guard + committed + guard;
#ifdef _WIN32
    auto* base = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, total, MEM_RESERVE, PAGE_NOACCESS));
    if (!base) return nullptr;
    if (!VirtualAlloc(base + guard, committed, MEM_COMMIT, PAGE_READWRITE)) {   // committed pages read as zero
        VirtualFree(base, 0, MEM_RELEASE);
        return nullptr;
    }
    return base;
#else
    int flags = MAP_PRIVATE | MAP_ANONYMOUS;
#ifdef MAP_NORESERVE
    flags |= MAP_NORESERVE;   // the guards are address space only, never memory
#endif
    void* m = mmap(nullptr, total, PROT_NONE, flags, -1, 0);
    if (m == MAP_FAILED) return nullptr;
    auto* base = static_cast<std::uint8_t*>(m);
    if (mprotect(base + guard, committed, PROT_READ | PROT_WRITE) != 0) {   // anonymous pages read as zero
        munmap(base, total);
        return nullptr;
    }
    return base;
#endif
}
}  // namespace

GuardedRegion::GuardedRegion(std::size_t size, std::size_t guard, const char* name) : size_(size) {
    const std::size_t ps = page_size();   // 4 KB, 16 KB on Apple silicon; both sizes used here are multiples
    guard_ = round_up(guard, ps);
    const std::size_t committed = round_up(size, ps);
    base_ = guard_ ? reserve_guarded(guard_, committed) : nullptr;
    if (!base_) {   // guards off, or not even their address space (a very low ulimit -v): the plain block, no guards
        guard_ = 0;
        base_ = reserve_guarded(0, committed);
        if (!base_) throw std::bad_alloc();
        data_ = base_;
        return;   // nothing to register
    }
    data_ = base_ + guard_;
    const std::size_t total = guard_ + committed + guard_;
    std::lock_guard<std::mutex> lock(g_guard_mutex);
    for (int k = 0; k < kGuardSlots; ++k) {
        GuardSlot& g = g_guard_slots[k];
        if (g.data.load(std::memory_order_relaxed) != 0) continue;
        g.lo = reinterpret_cast<std::uintptr_t>(base_);
        g.end = reinterpret_cast<std::uintptr_t>(data_) + size_;
        g.hi = reinterpret_cast<std::uintptr_t>(base_) + total;
        g.name = name;
        g.data.store(reinterpret_cast<std::uintptr_t>(data_), std::memory_order_release);
        slot_ = k;
        break;
    }   // (all slots taken: the guards still fault, the report only lacks the "overrun" line)
}

GuardedRegion::~GuardedRegion() {
    if (slot_ >= 0) {
        std::lock_guard<std::mutex> lock(g_guard_mutex);
        g_guard_slots[slot_].data.store(0, std::memory_order_release);
    }
#ifdef _WIN32
    VirtualFree(base_, 0, MEM_RELEASE);
#else
    munmap(base_, guard_ + round_up(size_, page_size()) + guard_);
#endif
}

bool guard_overrun(const void* p, char* out, std::size_t out_size) {
    const auto a = reinterpret_cast<std::uintptr_t>(p);
    for (const GuardSlot& g : g_guard_slots) {
        const std::uintptr_t data = g.data.load(std::memory_order_acquire);
        if (data == 0 || a < g.lo || a >= g.hi || (a >= data && a < g.end)) continue;
        if (a < data)
            std::snprintf(out, out_size, "guest memory overrun: host address %p is 0x%llx bytes before the start of %s",
                          p, static_cast<unsigned long long>(data - a), g.name);
        else
            std::snprintf(out, out_size, "guest memory overrun: host address %p is 0x%llx bytes past the end of %s",
                          p, static_cast<unsigned long long>(a - g.end), g.name);
        return true;
    }
    return false;
}

// 4 GB of guard on either side of RAM: any in-window pointer plus any 32-bit offset or length lands in a guard. A
// finite address-space limit (RLIMIT_AS: the checks' prlimit --as caps, which count reserved address space too) takes
// 1/128 of it for each guard (80 MB under the checks' 10 GB cap: native_play itself peaks near 8 GB of address space,
// mostly malloc's per-thread arenas); an overrun still faults at the first guard page, only a jump further than the
// guard can reach past it.
static std::size_t g_as_limit = 0;   // the RLIMIT_AS that ram_guard_size saw (0: none)
static std::size_t ram_guard_size() {
    std::size_t g = std::size_t{1} << 32;
#ifndef _WIN32
    rlimit rl{};
    if (getrlimit(RLIMIT_AS, &rl) == 0 && rl.rlim_cur != RLIM_INFINITY) {
        g_as_limit = static_cast<std::size_t>(rl.rlim_cur);
        if (g_as_limit / 128 < g) g = g_as_limit / 128;
    }
#endif
    return g < 0x10000u ? 0x10000u : g;
}
static std::string guards_off_reason(const char* asked) {
    if (asked && *asked) return asked;
    const char* e = std::getenv("B3_GUEST_GUARDS");
    return e && std::strcmp(e, "0") == 0 ? "B3_GUEST_GUARDS=0" : "";
}
NativeBackend::NativeBackend(const char* guards_off)
    : guards_off_(guards_off_reason(guards_off)),
      ram_(kMainRamSize, guards_off_.empty() ? ram_guard_size() : 0, "guest RAM"),
      spr_(kScratchpadSize, guards_off_.empty() ? 64u * 1024u : 0, "the scratchpad") {}

std::string NativeBackend::guard_text() const {
    if (!guards_off_.empty()) return "guest RAM guards: off (" + guards_off_ + ")";
    if (ram_.guard() == 0) return "guest RAM guards: none (the address space could not be reserved; plain allocation)";
    std::string t = "guest RAM guards: " + std::to_string(ram_.guard() >> 20) + " MB each";
    if (g_as_limit) t += " (RLIMIT_AS " + std::to_string(g_as_limit >> 20) + " MB)";
    return t + ", scratchpad " + std::to_string(spr_.guard() >> 10) + " KB";
}

void NativeBackend::clear() {
    std::memset(ram_.data(), 0, ram_.size());
    std::memset(spr_.data(), 0, spr_.size());
}

bool NativeBackend::load_elf(const std::string& path, std::string& error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) { error = "cannot open " + path; return false; }
    const std::vector<std::uint8_t> d((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return load_elf_bytes(d, path, error);
}

bool NativeBackend::load_elf_bytes(const std::vector<std::uint8_t>& d, const std::string& path, std::string& error) {
    auto rd32 = [&](std::size_t o) -> std::uint32_t {
        if (o + 4 > d.size()) return 0;
        std::uint32_t v;
        std::memcpy(&v, d.data() + o, 4);
        return v;
    };
    if (d.size() < 0x34 || std::memcmp(d.data(), "\x7f" "ELF", 4) != 0 || d[4] != 1 || d[5] != 1) {
        error = path + " is not a 32-bit little-endian ELF";
        return false;
    }
    clear();
    const std::uint32_t phoff = rd32(0x1C);
    const unsigned phnum = static_cast<unsigned>(d[0x2C] | (d[0x2D] << 8));
    unsigned loaded = 0;
    for (unsigned i = 0; i < phnum; ++i) {
        const std::size_t p = phoff + 32u * i;
        if (rd32(p) != 1) continue;   // PT_LOAD
        const std::uint32_t off = rd32(p + 4), va = rd32(p + 8), fsz = rd32(p + 16), msz = rd32(p + 20);
        if (msz == 0) continue;
        const std::uint32_t phys = va & 0x1FFFFFFFu;
        if (phys > ram_.size() || msz > ram_.size() - phys || fsz > msz || off + static_cast<std::size_t>(fsz) > d.size()) {
            error = "PT_LOAD " + hex32(va) + " does not fit the 32 MB arena";
            return false;
        }
        std::memcpy(ram_.data() + phys, d.data() + off, fsz);   // memsz beyond filesz stays zero (cleared above)
        ++loaded;
    }
    if (!loaded) { error = path + " has no PT_LOAD segment"; return false; }
    return true;
}

Windows NativeBackend::windows() {
    return Windows{ram_.data(), static_cast<std::uint32_t>(ram_.size()), spr_.data(), false};
}

void NativeBackend::read_slow(gaddr address, void*, std::uint32_t len) { throw NativeFault(address, len, false); }
void NativeBackend::write_slow(gaddr address, const void*, std::uint32_t len) { throw NativeFault(address, len, true); }
// boot1: hardware registers through b3hw_* go to the platform's model; without one they are a fault like any other
void NativeBackend::hw_write(gaddr address, unsigned width, const void* data) {
    if (!hw_model_) throw NativeFault(address, width, true);
    hw_model_->write(address, width, data);
}
void NativeBackend::hw_read(gaddr address, unsigned width, void* data) {
    if (!hw_model_) throw NativeFault(address, width, false);
    hw_model_->read(address, width, data);
}

void NativeBackend::call(gaddr function, const CallArgs& args, CallResult& result) {
    if (fallback) {
        // rt1 (the native HUD lost the objects drawn by callbacks that run retail): a watched function that
        // runs in the fallback is reported to the watch hook as a native entry would be (b3host/watch.h) -- before
        // (result null) and after. Observers only: no guest writes, no cost, so a proof run is unchanged
        const bool w = g_watch_hook != nullptr && function >= kWatchLo && function < kWatchHi && watch_bit(function);
        if (w) g_watch_hook(function, args, nullptr);
        fallback(function, args, result);
        if (w && g_watch_hook) g_watch_hook(function, args, &result);
        return;
    }
    throw MissingNative(function, args);
}

void NativeBackend::run(gaddr function, const CallArgs& args, CallResult& result) {
    const NativeOverride* n = native_override(function);
    if (!n) throw MissingNative(function, args);
    const gaddr saved = sp_;
    sp_ = args.sp;
    try {
        n->entry(args, result);   // registers the function does not return stay as the caller passed them in

    } catch (...) {
        sp_ = saved;
        throw;
    }
    if (sp_ != args.sp) {
        sp_ = saved;
        fatal("NativeBackend: " + hex32(function) + " did not restore the guest sp");
    }
    sp_ = saved;
}

}  // namespace b3host
