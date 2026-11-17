#include "b3host/null_backend.h"

#include <cstring>
#include <string>

namespace b3host {

NullBackend::NullBackend(std::uint32_t ram_size, bool record_stores)
    : ram_(ram_size, 0), spr_(kScratchpadSize, 0), record_stores_(record_stores),
      sp_((ram_size - 0x100u) & ~15u) {}   // [G] any 16-aligned address near the top of RAM; tests may set their own

Windows NullBackend::windows() {
    return Windows{ram_.data(), static_cast<std::uint32_t>(ram_.size()), spr_.data(), record_stores_};
}

void NullBackend::read_slow(gaddr address, void* dst, std::uint32_t len) {
    slow_log_.push_back({address, len, false});
    if (len > 8 || (address & (len - 1)) != 0) fatal("NullBackend: unaligned or wide slow read");
    const std::uint64_t cell = mmio_[address & ~7u];
    std::memcpy(dst, reinterpret_cast<const std::uint8_t*>(&cell) + (address & 7u), len);
}

void NullBackend::write_slow(gaddr address, const void* src, std::uint32_t len) {
    slow_log_.push_back({address, len, true});
    if (len > 8 || (address & (len - 1)) != 0) fatal("NullBackend: unaligned or wide slow write");
    std::uint64_t& cell = mmio_[address & ~7u];
    std::memcpy(reinterpret_cast<std::uint8_t*>(&cell) + (address & 7u), src, len);
}

void NullBackend::record_store(gaddr address, std::uint32_t len) { store_log_.push_back({address, len}); }

void NullBackend::call(gaddr function, const CallArgs& args, CallResult& result) {
    auto it = functions_.find(function);
    if (it == functions_.end()) fatal("NullBackend: no guest function at " + std::to_string(function));
    const gaddr sp = sp_;
    ++depth_;
    try {
        result = CallResult{};
        it->second(*this, args, result);
    } catch (...) {
        --depth_;
        throw;
    }
    --depth_;
    if (sp_ != sp) fatal("NullBackend: guest function did not restore sp");
}

void NullBackend::define_native(gaddr address, void (*entry)(const CallArgs&, CallResult&)) {
    functions_[address] = [entry](NullBackend&, const CallArgs& a, CallResult& r) { entry(a, r); };
}

}  // namespace b3host
