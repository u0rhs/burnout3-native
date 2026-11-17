// b3host/null_backend.h -- a backend over plain byte arrays, for unit tests and host-only tools.
//
// - main RAM: a byte vector (default 32 MB), scratchpad: 16 KB.
// - slow path: an "MMIO" word map; every slow access is logged.
// - record_store: optional; logged when enabled (tests check that stores are reported).
// - guest functions: there is no EE here, so a "guest function" is a host callback registered at a guest address
//   (define_function). call() runs it after checking the calling contract the real backends enforce (sp restored).
#ifndef B3HOST_NULL_BACKEND_H
#define B3HOST_NULL_BACKEND_H

#include "b3host/host.h"

#include <functional>
#include <map>
#include <vector>

namespace b3host {

class NullBackend : public Backend {
public:
    using GuestFunction = std::function<void(NullBackend&, const CallArgs&, CallResult&)>;

    struct SlowAccess { gaddr address; std::uint32_t len; bool write; };
    struct Store { gaddr address; std::uint32_t len; };

    explicit NullBackend(std::uint32_t ram_size = kMainRamSize, bool record_stores = false);

    Windows windows() override;
    void read_slow(gaddr address, void* dst, std::uint32_t len) override;
    void write_slow(gaddr address, const void* src, std::uint32_t len) override;
    void record_store(gaddr address, std::uint32_t len) override;
    gaddr stack_pointer() override { return sp_; }
    void set_stack_pointer(gaddr sp) override { sp_ = sp; }
    void call(gaddr function, const CallArgs& args, CallResult& result) override;
    void add_cost(std::uint32_t instructions) override { cost_ += instructions; }

    void define_function(gaddr address, GuestFunction fn) { functions_[address] = std::move(fn); }
    // Registers a native entry (b3host::native_entry<Fn>) as if an override were installed at `address`.
    void define_native(gaddr address, void (*entry)(const CallArgs&, CallResult&));

    std::vector<std::uint8_t>& ram() { return ram_; }
    std::vector<std::uint8_t>& scratchpad() { return spr_; }
    std::map<gaddr, std::uint64_t>& mmio() { return mmio_; }  // 8-byte cells keyed by 8-aligned address
    const std::vector<SlowAccess>& slow_log() const { return slow_log_; }
    const std::vector<Store>& store_log() const { return store_log_; }
    void clear_logs() { slow_log_.clear(); store_log_.clear(); }
    std::uint64_t cost() const { return cost_; }
    unsigned call_depth() const { return depth_; }

private:
    std::vector<std::uint8_t> ram_;
    std::vector<std::uint8_t> spr_;
    bool record_stores_;
    gaddr sp_;
    std::map<gaddr, std::uint64_t> mmio_;
    std::vector<SlowAccess> slow_log_;
    std::vector<Store> store_log_;
    std::map<gaddr, GuestFunction> functions_;
    std::uint64_t cost_ = 0;
    unsigned depth_ = 0;
};

}  // namespace b3host

#endif
