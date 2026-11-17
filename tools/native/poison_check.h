// poison_check.h -- the downstream test of words a native draft writes differently from retail but believes unread
// (mac5, 2026-11-09; the compare-mask policy's poison proof).
//
// B3NATIVE_POISON=<fn hex>:<lo hex>-<hi hex>:<flag hex>  after every call of the native function fn, if the byte at
// a0 + flag is 0, the words a0 + lo .. a0 + hi (exclusive) are filled with 0x7F7FFFFF (a large float). Run a scripted
// race with and without it: identical race shots (and pad / fallback logs) show nothing downstream reads those words in
// that state. CollideWithWorld: 1AB6B0:160-1B0:212 (the contact outputs when there is no contact, +0x212 = 0).
#ifndef B3NATIVE_POISON_CHECK_H
#define B3NATIVE_POISON_CHECK_H

#include "b3host/host.h"
#include "hook_table.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace b3native {

class PoisonCheck {
public:
    static bool requested() { const char* e = std::getenv("B3NATIVE_POISON"); return e && *e; }

    // on the hook table's port layer
    bool install(HookTable& hooks) {
        unsigned fn = 0, lo = 0, hi = 0, flag = 0;
        if (std::sscanf(std::getenv("B3NATIVE_POISON"), "%x:%x-%x:%x", &fn, &lo, &hi, &flag) != 4 || hi <= lo) {
            std::fprintf(stderr, "poison: B3NATIVE_POISON=<fn>:<lo>-<hi>:<flag>\n");
            return false;
        }
        if (!hooks.has_row(fn)) { std::fprintf(stderr, "poison: %08X has no native entry\n", fn); return false; }
        std::string error;
        if (!hooks.install("port:poison_check", {{fn, HookLayer::kPort, &trampoline, &native_}}, &error)) {
            std::fprintf(stderr, "poison: %s\n", error.c_str());
            return false;
        }
        fn_ = fn; lo_ = lo; hi_ = hi; flag_ = flag;
        instance() = this;
        std::printf("poison: %08X: a0+%X..+%X filled when a0+%X is 0, after each call\n", fn, lo, hi, flag);
        return true;
    }
    void report(std::FILE* out) const {
        std::fprintf(out, "poison: %08X: %llu calls, %llu poisoned\n", fn_, static_cast<unsigned long long>(calls_),
                     static_cast<unsigned long long>(poisoned_));
    }

private:
    static PoisonCheck*& instance() { static PoisonCheck* p = nullptr; return p; }
    static void trampoline(const b3host::CallArgs& a, b3host::CallResult& r) {
        PoisonCheck* p = instance();
        p->native_(a, r);
        ++p->calls_;
        const b3host::gaddr self = a.gpr[0].w[0] & 0x01FFFFFFu;
        if (b3host::load<std::uint8_t>(self + p->flag_) == 0) {
            for (unsigned o = p->lo_; o + 4 <= p->hi_; o += 4) b3host::store<std::uint32_t>(self + o, 0x7F7FFFFFu);
            ++p->poisoned_;
        }
    }
    void (*native_)(const b3host::CallArgs&, b3host::CallResult&) = nullptr;
    unsigned fn_ = 0, lo_ = 0, hi_ = 0, flag_ = 0;
    std::uint64_t calls_ = 0, poisoned_ = 0;
};

}  // namespace b3native

#endif
