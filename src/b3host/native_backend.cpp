// b3host native backend (S0): see include/b3host/native_backend.h and docs/standalone.md §8.
#include "b3host/native_backend.h"
#include "b3host/watch.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>

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

NativeBackend::NativeBackend() : ram_(kMainRamSize, 0), spr_(kScratchpadSize, 0) {}

void NativeBackend::clear() {
    std::fill(ram_.begin(), ram_.end(), 0);
    std::fill(spr_.begin(), spr_.end(), 0);
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
