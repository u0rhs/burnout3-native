// diff_check.h -- a per-call differential check of native functions against retail code (mac5, 2026-11-08): the
// proof a nonmatching draft needs before it may run in the product (the drafts run the physics and the crash effects,
// which must not change).
//
// B3NATIVE_DIFF=<hex>,<hex>,... (native_play): each listed function's native entry is wrapped (the hook table's
// port layer, hook_table.h). A call
//   1. saves guest RAM, the scratchpad and the VU0 state;
//   2. runs the retail code in the fallback's mini (NativeFallback::run) and keeps what it left;
//   3. restores the saved state and runs the native version;
//   4. compares the two: RAM outside the stack scratch below the caller's sp (both versions use their own frames
//      there), the scratchpad, v0 / v1 / f0;
//   5. continues from the retail result, so the run stays retail-faithful whatever the native version did.
// Differences are counted per function; the first ones are printed with the call number and the words. Also needs
// the fallback (not --strict). The retail run's mini retail_for()s the address, so retail code that calls the function
// itself runs retail, not the wrapper.
//
// Caveats (a reported difference to read with care, not a proof of a bug): both runs advance virtual time, so an
// interrupt or a time event (a VBlank handler, an IOP reply) can land in one run and not the other; such words show
// as differences in their handlers' data. B3NATIVE_DIFF_IGNORE=<hex lo>-<hex hi>,... leaves those ranges out.
#ifndef B3NATIVE_DIFF_CHECK_H
#define B3NATIVE_DIFF_CHECK_H

#include "hook_table.h"
#include "native_fallback.h"

#include "b3host/host.h"
#include "b3host/native_backend.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace b3native {

class DiffCheck {
public:
    static std::string requested() {
        const char* e = std::getenv("B3NATIVE_DIFF");
        return e ? std::string(e) : std::string();
    }

    bool install(b3host::NativeBackend& nb, NativeFallback& fb, const std::string& list, const TableEntry* entries,
                 std::size_t count, HookTable& hooks) {
        if (!nb.fallback || count == 0) { std::fprintf(stderr, "diff: needs the fallback\n"); return false; }
        nb_ = &nb;
        fb_ = &fb;
        std::vector<Hook> set;
        fns_.reserve(kSlots);      // the hooks' next pointers are into these vectors: no reallocation
        traced_.reserve(kSlots);
        for (const std::string& tok : split(list, ',')) {
            const auto a = static_cast<b3host::gaddr>(std::stoul(tok, nullptr, 16));
            if (!hooks.has_row(a)) { std::fprintf(stderr, "diff: %08X has no native entry\n", a); return false; }
            if (fns_.size() >= kSlots) { std::fprintf(stderr, "diff: at most %u functions\n", kSlots); return false; }
            Fn f;
            f.address = a;
            for (std::size_t e = 0; e < count; ++e) if (entries[e].address == a) f.ret = entries[e].ret;
            fns_.push_back(f);
            set.push_back({a, HookLayer::kPort, kTrampolines[fns_.size() - 1], &fns_.back().native});
            fb.retail_for(a);
        }
        // B3NATIVE_DIFF_TRACE=<hex>,...: the arguments of these callees (a0-t3 low words, f12-f19) are recorded during
        // both runs of a checked call; the first call that differs is printed (where a draft's computation goes wrong)
        if (const char* e = std::getenv("B3NATIVE_DIFF_TRACE"))
            for (const std::string& tok : split(e, ',')) {
                const auto a = static_cast<b3host::gaddr>(std::stoul(tok, nullptr, 16));
                if (!hooks.has_row(a)) { std::fprintf(stderr, "diff: trace %08X has no native entry\n", a); return false; }
                if (traced_.size() >= kSlots) break;
                traced_.push_back({a, nullptr});
                set.push_back({a, HookLayer::kPort, kTraceTrampolines[traced_.size() - 1], &traced_.back().entry});
            }
        if (const char* e = std::getenv("B3NATIVE_DIFF_IGNORE"))
            for (const std::string& r : split(e, ',')) {
                const std::size_t d = r.find('-');
                if (d == std::string::npos) continue;
                ignore_.push_back({static_cast<b3host::gaddr>(std::stoul(r.substr(0, d), nullptr, 16)),
                                   static_cast<b3host::gaddr>(std::stoul(r.substr(d + 1), nullptr, 16))});
            }
        std::string error;
        if (!hooks.install("port:diff_check", set, &error)) { std::fprintf(stderr, "diff: %s\n", error.c_str()); return false; }
        instance() = this;
        for (const Fn& f : fns_)
            std::printf("diff: %08X %s checked against retail at every call\n", f.address, b3host::describe_function(f.address).c_str());
        std::fflush(stdout);
        return true;
    }

    void report(std::FILE* out) const {
        if (!hist_.empty()) {
            std::fprintf(out, "diff: differing words over all calls (offset from a0, or absolute):");
            for (const auto& [k, n] : hist_)
                if (k >= 0x40000000L) std::fprintf(out, " [%08lX x%llu]", k - 0x40000000L, static_cast<unsigned long long>(n));
                else std::fprintf(out, " [a0%c%lX x%llu]", k < 0 ? '-' : '+', k < 0 ? -k : k, static_cast<unsigned long long>(n));
            std::fprintf(out, "\n");
        }
        for (const Fn& f : fns_)
            std::fprintf(out, "diff: %08X %s: %llu calls, %llu with a difference (RAM %llu, scratchpad %llu, result %llu)\n",
                         f.address, b3host::describe_function(f.address).c_str(), static_cast<unsigned long long>(f.calls),
                         static_cast<unsigned long long>(f.differing), static_cast<unsigned long long>(f.ram),
                         static_cast<unsigned long long>(f.spr), static_cast<unsigned long long>(f.result));
    }

private:
    static constexpr unsigned kSlots = 8;
    struct Fn {
        b3host::gaddr address = 0;
        void (*native)(const b3host::CallArgs&, b3host::CallResult&) = nullptr;
        std::uint64_t calls = 0, differing = 0, ram = 0, spr = 0, result = 0;
        unsigned printed = 0, printed_result = 0;
        char ret = 'q';   // the result register callers read (TableEntry::ret: n none, i / l v0, q v0 128 bits, f f0)
    };
    struct Range { b3host::gaddr lo, hi; };

    struct Traced { b3host::gaddr address; void (*entry)(const b3host::CallArgs&, b3host::CallResult&); };
    struct CallRec { b3host::gaddr address; std::uint32_t gpr[8]; std::uint32_t fpr[8]; std::uint32_t deref[8][4]; };
    std::vector<Traced> traced_;
    std::vector<CallRec>* recording_ = nullptr;
    std::vector<CallRec> calls_retail_, calls_native_;
    template <unsigned K> static void trace_trampoline(const b3host::CallArgs& a, b3host::CallResult& r) {
        DiffCheck* d = instance();
        if (d->recording_) {
            CallRec c{d->traced_[K].address, {}, {}, {}};
            const std::uint32_t sp = a.sp & 0x01FFFFFFu;
            for (unsigned i = 0; i < 8; ++i) {
                c.gpr[i] = a.gpr[i].w[0];
                c.fpr[i] = a.fpr[i];
                // a pointer into the caller's frame (a vector passed by address): its 16 bytes are compared, not the
                // address (the two versions' frames differ)
                const std::uint32_t p = c.gpr[i] & 0x01FFFFFFu;
                if (p >= sp && p < sp + 0x4000u && (c.gpr[i] >> 28) != 7) {
                    b3host::copy_from_guest(c.deref[i], p, 16);
                    c.gpr[i] = 0x5DACu;   // "stack": the address itself is not compared
                }
            }
            d->recording_->push_back(c);
        }
        d->traced_[K].entry(a, r);
    }
    static constexpr void (*kTraceTrampolines[kSlots])(const b3host::CallArgs&, b3host::CallResult&) = {
        &trace_trampoline<0>, &trace_trampoline<1>, &trace_trampoline<2>, &trace_trampoline<3>, &trace_trampoline<4>,
        &trace_trampoline<5>, &trace_trampoline<6>, &trace_trampoline<7>};
    static DiffCheck*& instance() { static DiffCheck* p = nullptr; return p; }
    template <unsigned K> static void trampoline(const b3host::CallArgs& a, b3host::CallResult& r) { instance()->call(K, a, r); }
    static constexpr void (*kTrampolines[kSlots])(const b3host::CallArgs&, b3host::CallResult&) = {
        &trampoline<0>, &trampoline<1>, &trampoline<2>, &trampoline<3>, &trampoline<4>, &trampoline<5>, &trampoline<6>, &trampoline<7>};

    static std::vector<std::string> split(const std::string& s, char c) {
        std::vector<std::string> out;
        std::size_t from = 0;
        while (from <= s.size()) {
            std::size_t to = s.find(c, from);
            if (to == std::string::npos) to = s.size();
            if (to > from) out.push_back(s.substr(from, to - from));
            from = to + 1;
        }
        return out;
    }
    bool ignored(b3host::gaddr a, b3host::gaddr sp) const {
        if (a < sp && a + 0x10000u >= sp) return true;   // the frames below the caller's sp: scratch for both versions
        for (const Range& r : ignore_) if (a >= r.lo && a < r.hi) return true;
        return false;
    }

    void call(unsigned k, const b3host::CallArgs& a, b3host::CallResult& r) {
        Fn& f = fns_[k];
        ++f.calls;
        std::vector<std::uint8_t>& ram = nb_->ram();
        std::vector<std::uint8_t>& spr = nb_->scratchpad();
        const b3asm::State vu0 = b3asm::state();
        saved_ram_ = ram;
        saved_spr_ = spr;
        b3host::CallResult rr{};
        calls_retail_.clear();
        calls_native_.clear();
        recording_ = &calls_retail_;
        fb_->run_retail(f.address, a, rr);   // retail
        recording_ = nullptr;
        retail_ram_ = ram;
        retail_spr_ = spr;
        const b3asm::State vu_retail = b3asm::state();
        std::memcpy(ram.data(), saved_ram_.data(), ram.size());
        std::memcpy(spr.data(), saved_spr_.data(), spr.size());
        b3asm::state() = vu0;
        b3host::CallResult rn{};
        recording_ = &calls_native_;
        f.native(a, rn);                // native
        recording_ = nullptr;
        const b3host::gaddr sp = a.sp & 0x01FFFFFFu;
        std::uint64_t ram_words = 0, spr_words = 0;
        std::vector<std::pair<b3host::gaddr, std::pair<std::uint32_t, std::uint32_t>>> first;
        for (std::size_t off = 0; off < ram.size(); off += 4096) {
            if (std::memcmp(ram.data() + off, retail_ram_.data() + off, 4096) == 0) continue;
            for (std::size_t w = off; w < off + 4096; w += 4) {
                std::uint32_t x, y;
                std::memcpy(&x, retail_ram_.data() + w, 4);
                std::memcpy(&y, ram.data() + w, 4);
                if (x == y || ignored(static_cast<b3host::gaddr>(w), sp)) continue;
                ++ram_words;
                if (first.size() < max_words_) first.push_back({static_cast<b3host::gaddr>(w), {x, y}});
                const long rel = long(w) - long(a.gpr[0].w[0] & 0x01FFFFFFu);
                ++hist_[(rel >= -0x2000 && rel < 0x2000) ? rel : 0x40000000L + long(w)];
            }
        }
        for (std::size_t w = 0; w < spr.size(); w += 4)
            if (std::memcmp(spr.data() + w, retail_spr_.data() + w, 4) != 0) ++spr_words;
        bool res = false;   // only the register a caller reads (a void function leaves v0 / f0 as whatever they were)
        if (f.ret == 'i') res = rr.v0.w[0] != rn.v0.w[0];
        else if (f.ret == 'l') res = rr.v0.w[0] != rn.v0.w[0] || rr.v0.w[1] != rn.v0.w[1];
        else if (f.ret == 'q') res = std::memcmp(&rr.v0, &rn.v0, sizeof rr.v0) != 0;
        else if (f.ret == 'f') res = rr.f0 != rn.f0;
        if (ram_words || spr_words || res) {
            ++f.differing;
            if (ram_words) ++f.ram;
            if (spr_words) ++f.spr;
            if (res) ++f.result;
            const bool memory = ram_words || spr_words;
            bool focused = focus_ < 0;
            for (std::size_t w = 0; !focused && w + 4 <= ram.size() && w < 0x2000000; w += 0) {
                const std::size_t at = (a.gpr[0].w[0] & 0x01FFFFFFu) + std::size_t(focus_);
                focused = at + 4 <= ram.size() && std::memcmp(ram.data() + at, retail_ram_.data() + at, 4) != 0;
                break;
            }
            if (focused && ((memory && f.printed < 12) || (!memory && f.printed_result < 3))) {
                ++(memory ? f.printed : f.printed_result);
                std::printf("diff: %08X call %llu: %llu RAM words, %llu scratchpad words%s (VBlank %llu, caller sp %08X, a0 %08X)", f.address,
                            static_cast<unsigned long long>(f.calls), static_cast<unsigned long long>(ram_words),
                            static_cast<unsigned long long>(spr_words), res ? ", result differs" : "",
                            static_cast<unsigned long long>(platform::vblank_count()), sp, a.gpr[0].w[0]);
                for (const auto& d : first) std::printf(" [%08X retail %08X native %08X]", d.first, d.second.first, d.second.second);
                std::printf("\n");
                if (!traced_.empty()) {
                    std::printf("diff:   traced calls: retail %zu, native %zu\n", calls_retail_.size(), calls_native_.size());
                    for (std::size_t c = 0; c < std::max(calls_retail_.size(), calls_native_.size()); ++c) {
                        const CallRec* x = c < calls_retail_.size() ? &calls_retail_[c] : nullptr;
                        const CallRec* y = c < calls_native_.size() ? &calls_native_[c] : nullptr;
                        if (x && y && x->address == y->address && std::memcmp(x->gpr, y->gpr, sizeof x->gpr) == 0 &&
                            std::memcmp(x->fpr, y->fpr, sizeof x->fpr) == 0 && std::memcmp(x->deref, y->deref, sizeof x->deref) == 0) continue;
                        for (const CallRec* z : {x, y}) {
                            if (!z) { std::printf("diff:   #%zu %s: (none)\n", c, z == x ? "retail" : "native"); continue; }
                            std::printf("diff:   #%zu %s %08X a0-t3", c, z == x ? "retail" : "native", z->address);
                            for (unsigned i = 0; i < 8; ++i) std::printf(" %08X", z->gpr[i]);
                            std::printf(" f12-f19");
                            for (unsigned i = 0; i < 8; ++i) std::printf(" %08X", z->fpr[i]);
                            for (unsigned i = 0; i < 8; ++i)
                                if (z->gpr[i] == 0x5DACu) std::printf(" *a%u=%08X %08X %08X %08X", i, z->deref[i][0], z->deref[i][1], z->deref[i][2], z->deref[i][3]);
                            std::printf("\n");
                        }
                        break;
                    }
                }
                std::fflush(stdout);
            }
        }
        // continue from retail's result
        std::memcpy(ram.data(), retail_ram_.data(), ram.size());
        std::memcpy(spr.data(), retail_spr_.data(), spr.size());
        b3asm::state() = vu_retail;
        r = rr;
    }

    b3host::NativeBackend* nb_ = nullptr;
    NativeFallback* fb_ = nullptr;
    std::vector<Fn> fns_;
    std::vector<Range> ignore_;
    std::map<long, std::uint64_t> hist_;
    long focus_ = [] { const char* e = std::getenv("B3NATIVE_DIFF_FOCUS"); return e ? long(std::strtol(e, nullptr, 16)) : -1L; }();   // print only calls differing at a0 + this offset   // differing word offset from a0 (within +-0x2000), else 0x40000000 + address
    std::size_t max_words_ = [] { const char* e = std::getenv("B3NATIVE_DIFF_WORDS"); return e ? std::size_t(std::strtoul(e, nullptr, 0)) : std::size_t(8); }();   // words printed per call
    std::vector<std::uint8_t> saved_ram_, saved_spr_, retail_ram_, retail_spr_;
};

}  // namespace b3native

#endif
