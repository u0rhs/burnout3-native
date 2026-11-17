// native_fallback.h -- the native boot's fallback (native3, Step 0; hook point agreed with boot2): a guest
// call from native code to a function with no native version runs retail in tests/r5900_mini.h instead of ending the
// run with MissingNative, and every call that retail code makes to a function with a native version runs natively.
// Every fallback is counted (function, calls, the native chain of its first call): the to-do list. native_boot
// installs it unless B3NATIVE_STRICT=1 (today's behaviour bit for bit: no fallback, MissingNative ends the run).
//
// One memory: the mini's RAM and scratchpad are the native backend's buffers (R5900::ext_ram / ext_spr).
// Per host thread (= per guest thread under platform_threads' baton) a stack of minis indexed by nesting depth, so a
// nested fallback (an interrupt handler, native -> retail -> native -> retail) never shares a register file, and a
// guest thread switch inside a mini run (add_cost -> time_check) leaves that thread's minis alone.
// At each transition: the argument registers a0-a3 / t0-t3 (128 bits), f12-f19 and sp (the native side's CallArgs),
// gp = 0x004E8670 (the game's _gp [C decomp config/b3_retail.yaml:5875, platform_sif kGp]), VU0 (b3asm::state(),
// thread_local, <-> mini.vu), and the results v0 (128 bits) / v1 / f0. Retail code's MMIO goes to the platform's
// HwModel (nb.hw_read / hw_write), mfc0 Count to the native virtual clock, COP0 Status.EIE is the native backend's
// one interrupt state (ei / di / mtc0 set it, delivering pending interrupts as native code does), and retired
// instructions become native cost every kTick instructions (virtual time: VBlanks and preemption happen inside long
// retail runs). A raw `syscall` in retail code (not through a native kernel wrapper) ends the run with a report.
// Costs differ from the static model: native_boot's poll-point runs with fallbacks active say so.
//
// nofallback1 (2026-11-14): compiled in only when B3_NATIVE_FALLBACK=1 (make NATIVE_FALLBACK=1: developer and diagnostic
// builds -- the exploration stand-ins, B3NATIVE_DIFF; native_boot always has it). The default, and every release build
// (Windows, Linux, Mac), is B3_NATIVE_FALLBACK=0: no R5900 interpreter and no retail execution in the binary (no
// tests/r5900_mini.h), and NativeFallback below is an empty stand-in that nothing installs -- a call to a function with
// no native version ends the run with MissingNative, which native_play turns into a clean stop with a message.
#ifndef B3NATIVE_NATIVE_FALLBACK_H
#define B3NATIVE_NATIVE_FALLBACK_H

#include "b3host/native_backend.h"
#include "native_table.h"
#include "platform.h"
#include "platform_vu0.h"

#ifndef B3_NATIVE_FALLBACK
#define B3_NATIVE_FALLBACK 0
#endif
#if B3_NATIVE_FALLBACK
#include "r5900_mini.h"
#endif

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace b3native {

inline std::string fb_hex32(std::uint32_t v) {
    char b[16];
    std::snprintf(b, sizeof b, "%08X", v);
    return b;
}

struct FallbackRecord {
    std::uint64_t calls = 0;
    std::uint64_t retired = 0;   // retail instructions run in the mini for it (its own and its retail callees')
    std::string first_chain;
};

#if B3_NATIVE_FALLBACK

// vu0: the mini that last started running retail code (any thread), for native_boot's watchdog report
inline std::atomic<mini::R5900*>& active_mini() {
    static std::atomic<mini::R5900*> m{nullptr};
    return m;
}
// vu0 diagnostics: B3NATIVE_PCCOUNT's counters
inline std::map<std::uint32_t, std::uint64_t>* pc_counts() {
    static auto* m = new std::map<std::uint32_t, std::uint64_t>;
    return m;
}
// vu0: every mini the fallback made (in order), for the watchdog report (read without locking: diagnostics only)
inline std::vector<mini::R5900*>& all_minis() {
    static std::vector<mini::R5900*> v;
    return v;
}

class NativeFallback {
public:
    static constexpr std::uint32_t kGp = 0x004E8670u;
    static constexpr std::uint64_t kTick = 4096;

    explicit NativeFallback(b3host::NativeBackend& nb) : nb_(nb) {
        // vu0: the DMAC model's clock sees the instructions this thread's running minis retired but not yet charged
        platform::guest_pending_cycles() = [] {
            std::uint64_t n = 0;
            const ThreadMinis& t = minis();
            for (unsigned k = 0; k < t.depth && k < t.stack.size(); ++k) n += t.stack[k]->unticked();
            return n;
        };
        std::size_t n = 0;
        const TableEntry* e = install(n);
        bits_.assign((0x2000000u >> 2) / 8 + 1, 0);
        for (std::size_t k = 0; k < n; ++k) {
            if (!b3host::native_override(e[k].address)) continue;   // native3 (S2): switched off (--retail): retail runs
            const std::uint32_t w = (e[k].address & 0x1FFFFFFu) >> 2;
            bits_[w / 8] |= static_cast<std::uint8_t>(1u << (w & 7));
            natives_.push_back(e[k].address);
        }
    }
    // mac5 (tools/native/diff_check.h): one call of `f` as retail code in the mini, the per-call check's reference run
    // (after retail_for(f), so the mini does not call back into the native version)
    void run_retail(b3host::gaddr f, const b3host::CallArgs& a, b3host::CallResult& r) { run(f, a, r); }
    // play1 (exploration, explore_standins.h RetailFor): `a` runs as retail code in the mini, not natively -- its mini
    // hook is dropped (else the mini, entering it, would call straight back to the switched-off native version). The
    // caller switches the native entry off too. Unused unless asked for: nothing changes otherwise.
    void retail_for(b3host::gaddr a) {
        const std::uint32_t w = (a & 0x1FFFFFFu) >> 2;
        bits_[w / 8] &= static_cast<std::uint8_t>(~(1u << (w & 7)));
        natives_.erase(std::remove(natives_.begin(), natives_.end(), a), natives_.end());
    }
    // sync1 (boot2's static-initialiser switch): a native entry's mini hook off / on again, natives_ untouched -- while
    // its native version is switched off, retail code in a mini runs the function itself instead of calling back into
    // a native that is not there (MissingNative)
    void hook_off(b3host::gaddr a) {
        const std::uint32_t w = (a & 0x1FFFFFFu) >> 2;
        bits_[w / 8] &= static_cast<std::uint8_t>(~(1u << (w & 7)));
    }
    void hook_on(b3host::gaddr a) {
        const std::uint32_t w = (a & 0x1FFFFFFu) >> 2;
        bits_[w / 8] |= static_cast<std::uint8_t>(1u << (w & 7));
    }
    void install_into_backend() {
        nb_.fallback = [this](b3host::gaddr f, const b3host::CallArgs& a, b3host::CallResult& r) { run(f, a, r); };
    }
    const std::map<b3host::gaddr, FallbackRecord>& log() const { return log_; }

    std::uint64_t total_calls() const { return total_; }

    // The summary native_boot prints at the end (loud: every function, its calls, the chain of its first call).
    void report(std::FILE* out) const {
        std::fprintf(out, "fallback: %zu functions ran retail in the mini (%llu calls) [B3NATIVE_STRICT=1 halts instead]\n",
                     log_.size(), static_cast<unsigned long long>(total_));
        for (const auto& [a, rec] : log_)
            std::fprintf(out, "fallback: %08X %s: %llu calls, %llu retail instructions; first from: %s\n", a,
                         b3host::describe_function(a).c_str(), static_cast<unsigned long long>(rec.calls),
                         static_cast<unsigned long long>(rec.retired), rec.first_chain.c_str());
    }

private:
    b3host::NativeBackend& nb_;
    std::vector<std::uint8_t> bits_;
    std::vector<b3host::gaddr> natives_;
    std::map<b3host::gaddr, FallbackRecord> log_;
    std::uint64_t total_ = 0;

    // vu0: a mini's tick comes no later than the next queued DMAC completion's ready time (the model's clock), so the
    // callback is delivered at its time inside a long retail run; kTick otherwise
    // boot2: and no later than the backend's next time event (the platform's arm_time: a VBlank, an alarm, a SIF
    // reply -- 0 when one is queued, as after a native sceSifCallRpc whose reply is due at the next scheduler step:
    // CGtFilePS2DVD::SetPosition's seek completion was late by up to kTick instructions, poll 3014's
    // CGtFilePS2DVD +0x18 = 2 for the recorded 1)
    void retick(mini::R5900& m) {
        std::uint64_t t = kTick;
        const std::uint64_t r = platform::hw_model().next_completion_ready();
        if (r != ~std::uint64_t{0} && platform::hw_model().clock) {
            const std::uint64_t now = platform::hw_model().clock();
            const std::uint64_t d = r > now ? r - now : 1;
            if (d < t) t = d;
        }
        if (nb_.time_event_ != ~std::uint64_t{0}) {
            const std::uint64_t done = nb_.cost() + m.unticked();
            const std::uint64_t d = nb_.time_event_ > done ? nb_.time_event_ - done : 1;
            if (d + m.unticked() < t) t = d + m.unticked();   // tick_every counts from the last tick
        }
        m.tick_every = t;
    }
    // vu0: the backend's guest sp at a mini's sp (16-aligned, 64 bytes below) for the scope
    struct MiniSp {
        b3host::NativeBackend& nb;
        const b3host::gaddr saved;
        MiniSp(b3host::NativeBackend& n, const mini::R5900& m)
            : nb(n), saved(n.stack_pointer()) { nb.set_stack_pointer((static_cast<std::uint32_t>(m.gpr_lo(29)) - 64u) & ~15u); }
        ~MiniSp() { nb.set_stack_pointer(saved); }
    };
    struct ThreadMinis {
        std::vector<std::unique_ptr<mini::R5900>> stack;
        unsigned depth = 0;
    };
    static ThreadMinis& minis() {
        thread_local ThreadMinis t;
        return t;
    }

    mini::R5900& make_mini() {
        auto m = std::make_unique<mini::R5900>();
        m->ext_ram = nb_.ram().data();
        m->ext_spr = nb_.scratchpad().data();
        m->hook_bits = &bits_;
        m->max_steps = ~0ull;
        m->trap_low_pc = true;
        // vu0 diagnostics: B3NATIVE_PCCOUNT=<pc>,<pc>,... (hex): how often retail code in any mini executes each pc
        // (printed at exit)
        if (const char* pcs = std::getenv("B3NATIVE_PCCOUNT")) {
            static std::map<std::uint32_t, std::uint64_t>* counts = [] {
                auto* c = new std::map<std::uint32_t, std::uint64_t>;
                std::atexit([] {
                    for (const auto& [pc, n] : *pc_counts()) std::printf("pccount: %08X %s %llu\n", pc, b3host::describe_function(pc).c_str(), (unsigned long long)n);
                });
                return c;
            }();
            (void)counts;
            for (const char* c = pcs; *c;) {
                char* e = nullptr;
                const std::uint32_t pc = static_cast<std::uint32_t>(std::strtoul(c, &e, 16));
                if (e == c) break;
                (*pc_counts())[pc];
                m->probes[pc] = [pc](mini::R5900&) { ++(*pc_counts())[pc]; };
                c = *e == ',' ? e + 1 : e;
            }
        }
        // vu0 diagnostics: B3NATIVE_VFTAINT=<pc> (hex): when retail code reaches pc (a function's entry), every VF register's
        // w lane becomes 0x7F80F000 | r and every GPR's top word (bits 96-127, r != 0 / sp / ra) 0x7F80E000 | r: a stored lane
        // that holds one of these came from that entry register unchanged (the dynamic half of the register-lane class)
        if (const char* v = std::getenv("B3NATIVE_VFTAINT")) {
            const std::uint32_t pc = static_cast<std::uint32_t>(std::strtoul(v, nullptr, 16));
            m->probes[pc] = [](mini::R5900& c) {
                for (unsigned r = 1; r < 32; ++r) c.vu.vf[r].w[3] = 0x7F80F000u | r;
                for (unsigned r = 1; r < 32; ++r) {
                    if (r == 29 || r == 31) continue;
                    b3asm::Q q = c.gpr128(r);
                    q.w[3] = 0x7F80E000u | r;
                    c.set_gpr128(r, q);
                }
            };
        }
        // vu0 diagnostics: B3NATIVE_STACKTAINT=<pc>,<frame> (hex): when retail code reaches pc (a function's entry, before its
        // `addiu $sp, $sp, -frame`), the frame-to-be's words get 0x7FD00000 | X, X = the offset from the function's own sp:
        // a stored lane holding one came from that never-written stack word (the dynamic half of the stack class)
        if (const char* v = std::getenv("B3NATIVE_STACKTAINT")) {
            const std::uint32_t pc = static_cast<std::uint32_t>(std::strtoul(v, nullptr, 16));
            const char* comma = std::strchr(v, ',');
            const std::uint32_t frame = comma ? static_cast<std::uint32_t>(std::strtoul(comma + 1, nullptr, 16)) : 0;
            std::uint8_t* ram = nb_.ram().data();
            m->probes[pc] = [ram, frame](mini::R5900& c) {
                const std::uint32_t sp = static_cast<std::uint32_t>(c.gpr_lo(29)) & 0x01FFFFFFu, base = sp - frame;
                for (std::uint32_t x = 0; x < frame; x += 4) { const std::uint32_t w = 0x7FD00000u | x; std::memcpy(ram + base + x, &w, 4); }
            };
        }
        // vu0 diagnostics: B3NATIVE_STACKSET=<pc>,<frame>,<offset>,<value> (hex): when retail code reaches pc (a function's
        // entry), the word at the frame-to-be's offset gets value (a never-written slot given the mixed native / retail run's stale value:
        // does a stored result depend on it?)
        if (const char* v = std::getenv("B3NATIVE_STACKSET")) {   // several: ';'-separated, the same pc and frame
            std::vector<std::array<std::uint32_t, 4>> specs;
            for (const char* c = v; c && *c;) {
                std::array<std::uint32_t, 4> f{};
                for (int i = 0; i < 4 && c && *c; ++i) { char* e = nullptr; f[i] = static_cast<std::uint32_t>(std::strtoul(c, &e, 16)); c = (*e == ',') ? e + 1 : e; }
                specs.push_back(f);
                c = (c && *c == ';') ? c + 1 : nullptr;
            }
            std::uint8_t* ram = nb_.ram().data();
            if (!specs.empty())
                m->probes[specs[0][0]] = [ram, specs](mini::R5900& cpu) {
                    const std::uint32_t sp = static_cast<std::uint32_t>(cpu.gpr_lo(29)) & 0x01FFFFFFu;
                    for (const auto& f : specs) std::memcpy(ram + (sp - f[1] + f[2]), &f[3], 4);
                };
        }
        // vu0 diagnostics: B3NATIVE_VFSET=<pc>,<file> (file: B3NATIVE_VFDUMP lines "vfdump <pc> #n vfR x y z w"): when retail
        // code reaches pc, those VF registers get those values (first hit only): a caller's VF state transplanted
        if (const char* v = std::getenv("B3NATIVE_VFSET")) {
            const std::uint32_t pc = static_cast<std::uint32_t>(std::strtoul(v, nullptr, 16));
            const char* comma = std::strchr(v, ',');
            std::vector<std::pair<unsigned, std::array<std::uint32_t, 4>>> regs;
            if (comma)
                if (std::FILE* f = std::fopen(comma + 1, "r")) {
                    char line[512];
                    while (std::fgets(line, sizeof line, f)) {
                        unsigned r = 0, hit = 0, p = 0; std::array<std::uint32_t, 4> q{};
                        if (std::sscanf(line, "vfdump %x #%u vf%u %x %x %x %x", &p, &hit, &r, &q[0], &q[1], &q[2], &q[3]) == 7 && hit == 1 && r < 32)
                            regs.push_back({r, q});
                    }
                    std::fclose(f);
                }
            m->probes[pc] = [regs](mini::R5900& c) {
                static bool done = false;
                if (done) return;
                done = true;
                for (const auto& [r, q] : regs) for (unsigned l = 0; l < 4; ++l) c.vu.vf[r].w[l] = q[l];
            };
        }
        // vu0 diagnostics: B3NATIVE_FRAMEDUMP=<pc>,<frame>,<file> / B3NATIVE_FRAMESET=<pc>,<frame>,<file>: when retail code
        // reaches pc (a function's entry) the first time, the frame-to-be [sp - frame, sp) is written to / read from file
        // (one run's stale frame transplanted into another: does a result depend only on it?)
        for (const char* key : {"B3NATIVE_FRAMEDUMP", "B3NATIVE_FRAMESET"}) {
            const char* v = std::getenv(key);
            if (!v) continue;
            char* e = nullptr;
            const std::uint32_t pc = static_cast<std::uint32_t>(std::strtoul(v, &e, 16));
            const std::uint32_t frame = (*e == ',') ? static_cast<std::uint32_t>(std::strtoul(e + 1, &e, 16)) : 0;
            const std::string file = (*e == ',') ? std::string(e + 1) : std::string();
            const bool dump = key[9] == 'F' && key[14] == 'D';
            std::uint8_t* ram = nb_.ram().data();
            auto prev = m->probes.count(pc) ? m->probes[pc] : std::function<void(mini::R5900&)>();
            m->probes[pc] = [ram, frame, file, dump, prev](mini::R5900& c) {
                if (prev) prev(c);
                static bool done[2] = {false, false};
                if (done[dump]) return;
                done[dump] = true;
                const std::uint32_t sp = static_cast<std::uint32_t>(c.gpr_lo(29)) & 0x01FFFFFFu;
                if (std::FILE* f = std::fopen(file.c_str(), dump ? "wb" : "rb")) {
                    if (dump) std::fwrite(ram + sp - frame, 1, frame, f);
                    else if (std::fread(ram + sp - frame, 1, frame, f) != frame) std::printf("FRAMESET: short file\n");
                    std::fclose(f);
                }
            };
        }
        // vu0 diagnostics: B3NATIVE_SPDUMP=<pc> (hex): $sp, $ra and $a0 the first 64 times retail code reaches pc
        if (const char* v = std::getenv("B3NATIVE_SPDUMP")) {
            const std::uint32_t pc = static_cast<std::uint32_t>(std::strtoul(v, nullptr, 16));
            auto prev = m->probes.count(pc) ? m->probes[pc] : std::function<void(mini::R5900&)>();
            m->probes[pc] = [pc, prev](mini::R5900& c) {
                if (prev) prev(c);
                static unsigned hits = 0;
                if (++hits <= 64) std::printf("spdump %08X #%u sp %08X ra %08X a0 %08X f0-f5 %08X %08X %08X %08X %08X %08X a1-a3 %08X %08X %08X\n", pc, hits, static_cast<std::uint32_t>(c.gpr_lo(29)), static_cast<std::uint32_t>(c.gpr_lo(31)), static_cast<std::uint32_t>(c.gpr_lo(4)), c.fpr(0), c.fpr(1), c.fpr(2), c.fpr(3), c.fpr(4), c.fpr(5), static_cast<std::uint32_t>(c.gpr_lo(5)), static_cast<std::uint32_t>(c.gpr_lo(6)), static_cast<std::uint32_t>(c.gpr_lo(7)));
            };
        }
        // vu0 diagnostics: B3NATIVE_VFDUMP=<pc> (hex): the VU0 macro registers (VF1-31) when retail code reaches pc, first
        // 4 times (the VF state a caller sees after a native callee returns, against an all-retail run's)
        if (const char* v = std::getenv("B3NATIVE_VFDUMP")) {
            const std::uint32_t pc = static_cast<std::uint32_t>(std::strtoul(v, nullptr, 16));
            m->probes[pc] = [pc](mini::R5900& c) {
                static unsigned hits = 0;
                if (++hits > 4) return;
                for (unsigned r = 1; r < 32; ++r)
                    std::printf("vfdump %08X #%u vf%u %08X %08X %08X %08X\n", pc, hits, r, c.vu.vf[r].w[0], c.vu.vf[r].w[1],
                                c.vu.vf[r].w[2], c.vu.vf[r].w[3]);
                std::fflush(stdout);
            };
        }
        // vu0 diagnostics: B3NATIVE_WATCH=<lo>-<hi> (hex): every retail store into the range, with its pc (first 200)
        if (const char* w = std::getenv("B3NATIVE_WATCH")) {
            m->watch_lo = static_cast<std::uint32_t>(std::strtoul(w, nullptr, 16));
            const char* dash = std::strchr(w, '-');
            m->watch_hi = dash ? static_cast<std::uint32_t>(std::strtoul(dash + 1, nullptr, 16)) : m->watch_lo + 4;
            m->on_watch = [](mini::R5900& c, std::uint32_t a, unsigned n) {
                static unsigned count = 0;
                if (++count <= 200)
                    std::printf("watch: store %u at %08X pc %08X %s ra %08X\n", n, a, c.pc(), b3host::describe_function(c.pc()).c_str(),
                                static_cast<std::uint32_t>(c.gpr_lo(31)));
            };
        }
        // vu0 diagnostics: B3NATIVE_POISON=<file> (one hex RAM address per line, '#' comments; e.g. compare_mask.tsv's first
        // column): after every retail store into one of those words it is overwritten with 10000.0f (r5900_mini.h poison)
        if (const char* pf = std::getenv("B3NATIVE_POISON")) {
            static std::vector<std::uint32_t>* words = [pf] {
                auto* v = new std::vector<std::uint32_t>;
                if (std::FILE* f = std::fopen(pf, "r")) {
                    char line[4096];
                    while (std::fgets(line, sizeof line, f))
                        if (line[0] != '#' && std::isxdigit(static_cast<unsigned char>(line[0])))
                            v->push_back(static_cast<std::uint32_t>(std::strtoul(line, nullptr, 16)) & ~3u);
                    std::fclose(f);
                }
                std::printf("native_boot: B3NATIVE_POISON: %zu words\n", v->size());
                return v;
            }();
            if (!words->empty()) {
                m->poison = words;
                m->poison_lo = *std::min_element(words->begin(), words->end());
                m->poison_hi = *std::max_element(words->begin(), words->end()) + 4;
            }
        }
        // vu0 diagnostics: B3NATIVE_RWATCH=<lo>-<hi> (hex): every retail load AND store overlapping the range, in order, as
        // run-length events "rwatch: <load|store> <size> at <addr> pc <pc> ... x<count>" (a new line when the kind, size,
        // address or pc changes), with the poll-point count at the time (the reader question of config/compare_mask.tsv)
        if (const char* w = std::getenv("B3NATIVE_RWATCH")) {
            const std::uint32_t lo = static_cast<std::uint32_t>(std::strtoul(w, nullptr, 16));
            const char* dash = std::strchr(w, '-');
            const std::uint32_t hi = dash ? static_cast<std::uint32_t>(std::strtoul(dash + 1, nullptr, 16)) : lo + 4;
            struct Ev { bool store; unsigned n; std::uint32_t a, pc, ra; std::uint64_t count; };
            static Ev last{};
            static std::uint64_t lines = 0;
            static bool init = [] {
                std::atexit([] { if (last.count) std::printf("rwatch: (last event repeated x%llu)\n", (unsigned long long)last.count); std::fflush(stdout); });
                return true;
            }();
            (void)init;
            auto ev = [](mini::R5900& c, std::uint32_t a, unsigned n, bool store) {
                const std::uint32_t ra = static_cast<std::uint32_t>(c.gpr_lo(31));
                if (last.count && last.store == store && last.n == n && last.a == a && last.pc == c.pc()) { ++last.count; return; }
                if (last.count > 1) std::printf("rwatch:   x%llu\n", (unsigned long long)last.count);
                last = Ev{store, n, a, c.pc(), ra, 1};
                if (++lines <= 4000)
                    std::printf("rwatch: %s %u at %08X pc %08X %s ra %08X %s\n", store ? "store" : "load ", n, a, c.pc(),
                                b3host::describe_function(c.pc()).c_str(), ra, b3host::describe_function(ra).c_str());
            };
            if (!m->on_watch) { m->watch_lo = lo; m->watch_hi = hi; m->on_watch = [ev](mini::R5900& c, std::uint32_t a, unsigned n) { ev(c, a, n, true); }; }
            m->rwatch_lo = lo; m->rwatch_hi = hi;
            m->on_rwatch = [ev](mini::R5900& c, std::uint32_t a, unsigned n) { ev(c, a, n, false); };
        }
        m->tick_every = kTick;
        // vu0: what can deliver an interrupt from inside a retail run (virtual time, ei / mtc0, a DMA register write)
        // runs with the backend's guest sp at the mini's current sp, so a handler's frame (call_guest) goes below the
        // interrupted code's live frames, not over them from the stale sp of the last native -> retail boundary
        // (navproof14 poll-3000 restore, retail: rwDMAVSYNCHandler's saved ra overwritten, `jr $ra` to 0)
        mini::R5900* const mp = m.get();
        m->on_tick = [this, mp](std::uint64_t n) {
            MiniSp g(nb_, *mp);
            nb_.add_cost(static_cast<std::uint32_t>(n));
            retick(*mp);
        };
        m->count_source = [this] { return nb_.cop0_count(); };
        m->on_vcallms = [](mini::R5900& c, std::uint32_t entry) { platform::vu0_micro_call(c.vu, entry, c.pc()); };
        m->status_eie = [this] { return nb_.interrupts_enabled(); };
        m->on_eie = [this, mp](bool on) { MiniSp g(nb_, *mp); nb_.set_interrupts_enabled(on); retick(*mp); };
        m->on_mmio = [this, mp](std::uint32_t phys, unsigned width, bool write, std::uint8_t* data) {
            MiniSp g(nb_, *mp);
            if (write) nb_.hw_write(phys, width, data);
            else nb_.hw_read(phys, width, data);
            retick(*mp);
        };
        m->on_syscall = [](mini::R5900& c) {
            b3host::fatal("native fallback: retail code made a raw syscall at pc " + fb_hex32(c.pc() - 4) + " " +
                          b3host::describe_function(c.pc() - 4) + " (not through a native kernel wrapper)");
        };
        mini::R5900* self = m.get();
        for (const b3host::gaddr a : natives_)
            m->hooks[a] = [this, self, a](mini::R5900& c) { native_from_mini(c, a); (void)self; };
        mini::R5900& ref = *m;
        {
            static std::mutex mu;
            std::lock_guard<std::mutex> lk(mu);
            all_minis().push_back(&ref);
        }
        minis().stack.push_back(std::move(m));
        return ref;
    }

    static void to_vu(mini::R5900& m) {
        const b3asm::State& s = b3asm::state();
        for (unsigned r = 1; r < 32; ++r) m.vu.vf[r] = s.vf[r];
        m.vu.acc = s.acc; m.vu.q = s.q; m.vu.i = s.i; m.vu.r = s.r;
        for (unsigned r = 0; r < 16; ++r) m.vu.vi[r] = s.vi[r];
    }
    static void from_vu(const mini::R5900& m) {
        b3asm::State& s = b3asm::state();
        for (unsigned r = 1; r < 32; ++r) s.vf[r] = m.vu.vf[r];
        s.acc = m.vu.acc; s.q = m.vu.q; s.i = m.vu.i; s.r = m.vu.r;
        for (unsigned r = 0; r < 16; ++r) s.vi[r] = m.vu.vi[r];
    }

    // Retail code in the mini reached a function with a native version: run it natively, as the mini's hook.
    void native_from_mini(mini::R5900& c, b3host::gaddr a) {
        b3host::CallArgs args;
        for (unsigned i = 0; i < 8; ++i) { args.gpr[i] = c.gpr128(4 + i); args.fpr[i] = c.fpr(12 + i); }
        args.n_gpr = args.n_fpr = 8;
        args.sp = static_cast<std::uint32_t>(c.gpr_lo(29));
        b3host::CallResult r;
        r.v0 = c.gpr128(2);
        r.v1 = c.gpr_lo(3);
        r.f0 = c.fpr(0);
        from_vu(c);
        nb_.run(a, args, r);
        retick(c);   // boot2: the native call may have armed a time event (a queued SIF reply)
        to_vu(c);
        c.set_gpr128(2, r.v0);
        c.set_gpr64(3, r.v1);
        c.set_fpr(0, r.f0);
    }

    // A native guest call with no native function.
    void run(b3host::gaddr f, const b3host::CallArgs& a, b3host::CallResult& r) {
        FallbackRecord& rec = log_[f];
        if (rec.calls++ == 0) rec.first_chain = b3host::native_chain::format(b3host::native_chain::snapshot());
        b3host::native_chain::Scope chain_scope(f);   // mac4: a retail function is a link in the chain of an error below it
        ++total_;
        ThreadMinis& t = minis();
        struct Depth {
            ThreadMinis& t;
            ~Depth() { --t.depth; }
        };
        mini::R5900& m = t.depth < t.stack.size() ? *t.stack[t.depth] : make_mini();
        ++t.depth;
        Depth guard{t};
        for (unsigned i = 0; i < 8; ++i) {
            m.set_gpr128(4 + i, i < a.n_gpr ? a.gpr[i] : b3asm::Q{});
            m.set_fpr(12 + i, i < a.n_fpr ? a.fpr[i] : 0u);
        }
        m.set_gpr64(29, static_cast<std::uint64_t>(static_cast<std::int64_t>(static_cast<std::int32_t>(a.sp))));
        m.set_gpr64(28, kGp);
        to_vu(m);
        const std::uint64_t before = m.total_retired;
        mini::R5900* const prev = active_mini().exchange(&m);
        retick(m);   // boot2: an event already armed when the retail run starts
        m.run_nested(f);
        active_mini() = prev;
        nb_.add_cost(static_cast<std::uint32_t>(m.untick()));   // the instructions since the last tick
        from_vu(m);
        const std::uint64_t n = m.total_retired - before;
        rec.retired += n;
        if (static_cast<std::uint32_t>(m.gpr_lo(29)) != a.sp)
            b3host::fatal("native fallback: " + fb_hex32(f) + " " + b3host::describe_function(f) + " did not restore sp");
        r.v0 = m.gpr128(2);
        r.v1 = m.gpr_lo(3);
        r.f0 = m.fpr(0);
    }
};

#else  // B3_NATIVE_FALLBACK=0: compiled out

// The interface the exploration stand-ins and the diff check are written against, with nothing behind it: native_play
// never makes one in this build (its `fallback` stays null, so those tools refuse to start), and the backend's
// fallback hook stays empty -- NativeBackend::call throws MissingNative.
class NativeFallback {
public:
    explicit NativeFallback(b3host::NativeBackend&) {}
    void run_retail(b3host::gaddr f, const b3host::CallArgs&, b3host::CallResult&) {
        b3host::fatal("native fallback: " + fb_hex32(f) + " " + b3host::describe_function(f) +
                      " would run as retail code, but this build has no fallback (NATIVE_FALLBACK=0)");
    }
    void retail_for(b3host::gaddr) {}
    void hook_off(b3host::gaddr) {}
    void hook_on(b3host::gaddr) {}
    void install_into_backend() {}
    const std::map<b3host::gaddr, FallbackRecord>& log() const { return log_; }
    std::uint64_t total_calls() const { return 0; }
    void report(std::FILE* out) const { std::fprintf(out, "fallback: none (compiled out)\n"); }

private:
    std::map<b3host::gaddr, FallbackRecord> log_;
};

#endif  // B3_NATIVE_FALLBACK

}  // namespace b3native

#endif
