// time_trace.h -- a per-call timing trace of native entries (sif-lane, 2026-11-10): the golden check for a platform
// row that replaces a library function, where the replacement must charge the virtual EE time the old path charged
// (src/native/platform_charge.h) and answer the same values.
//
// B3NATIVE_TIME_TRACE=<hex>,<hex>,... (native_play): each listed native entry is wrapped (a bound hook on the hook
// table's port layer, hook_table.h); every call writes one line
//   <address> v<VBlank at entry> c<virtual EE cycles at entry>-<at exit> a <a0> <a1> <a2> <a3> -> <v0 low word>
// to $B3NATIVE_TIME_TRACE_FILE (default stderr). The cycles are the platform's virtual time (b3host cost counter plus
// the time a waiting thread skipped), so two builds whose traces are equal line for line called these functions at the
// same virtual times, spent the same time inside them (interrupt handlers that ran inside included) and returned the
// same values. The entry's own static cost is added by the dispatcher after the call returns, outside the line.
#ifndef B3NATIVE_TIME_TRACE_H
#define B3NATIVE_TIME_TRACE_H

#include "hook_table.h"
#include "platform.h"

#include "b3host/call.h"
#include "b3host/host.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace b3native {

class TimeTrace {
public:
    static bool requested() { const char* e = std::getenv("B3NATIVE_TIME_TRACE"); return e && *e; }

    bool install(HookTable& hooks) {
        const char* e = std::getenv("B3NATIVE_TIME_TRACE");
        if (!e || !*e) return false;
        std::string list(e);
        std::vector<b3host::gaddr> addrs;
        for (std::size_t at = 0; at <= list.size();) {
            const std::size_t comma = std::min(list.find(',', at), list.size());
            const std::string tok = list.substr(at, comma - at);
            at = comma + 1;
            if (tok.empty()) continue;
            const auto a = static_cast<b3host::gaddr>(std::stoul(tok, nullptr, 16));
            if (!hooks.has_row(a)) { std::fprintf(stderr, "time_trace: %08X has no native entry\n", a); return false; }
            if (addrs.size() >= kSlots) { std::fprintf(stderr, "time_trace: at most %u functions\n", kSlots); return false; }
            addrs.push_back(a);
        }
        slots_.reserve(addrs.size());   // the bound hooks keep a pointer to their slot
        std::vector<Hook> set;
        for (b3host::gaddr a : addrs) {
            slots_.push_back({a, this});
            set.push_back(bind_hook(a, HookLayer::kPort, &call, &slots_.back()));
        }
        const char* f = std::getenv("B3NATIVE_TIME_TRACE_FILE");
        out_ = f && *f ? std::fopen(f, "w") : stderr;
        if (!out_) { std::fprintf(stderr, "time_trace: cannot open %s\n", f); return false; }
        std::string error;
        if (!hooks.install("port:time_trace", set, &error)) { std::fprintf(stderr, "time_trace: %s\n", error.c_str()); return false; }
        std::fprintf(stderr, "time_trace: %zu functions traced\n", slots_.size());
        return true;
    }
    ~TimeTrace() {
        if (out_ && out_ != stderr) std::fclose(out_);
    }

private:
    static constexpr unsigned kSlots = 64;   // the 41 EE kernel rows of docs/specs/ee-kernel.md §12 in one run
    struct Slot {
        b3host::gaddr address;
        TimeTrace* trace;
    };
    static void call(const b3host::CallArgs& a, b3host::CallResult& r, void* user, HookEntry next) {
        const Slot& s = *static_cast<const Slot*>(user);
        const unsigned long long v = platform::vblank_count(), c0 = platform::virtual_cycles();
        next(a, r);
        const unsigned long long c1 = platform::virtual_cycles();
        std::fprintf(s.trace->out_, "%08X v%llu c%llu-%llu a %08X %08X %08X %08X -> %08X\n", s.address, v, c0, c1, a.gpr[0].w[0],
                     a.gpr[1].w[0], a.gpr[2].w[0], a.gpr[3].w[0], r.v0.w[0]);
    }
    std::vector<Slot> slots_;
    std::FILE* out_ = nullptr;
};

}  // namespace b3native

#endif
