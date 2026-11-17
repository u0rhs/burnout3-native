// explore_standins.h -- EXPLORATION ONLY stand-ins for the native boot (play1, 2026-11-07): ways past what the
// platform cannot do yet, so the boot can be followed further. Never part of a proof: off by default, every use
// logged loudly, and refused by native_boot in --poll-point / --oracle / --lookahead runs.
//
// Movie stand-in (B3NATIVE_MOVIE_STANDIN=1, native_play --explore-movies): every movie ends at once. The IPU is
// movie2's (libmpeg as C + the IPU model); until it decodes, CGtVideoDecoder::Update (0x325030, native) waits inside
// sceMpegGetPicture for pictures that never come. The stand-in answers the two calls Update makes per picture
// [C decomp src/nonmatching/func_00325030.cpp]:
//   sceMpegGetPicture (0x109F00): returns at once, no picture (the frame buffer keeps what it had);
//   sceMpegIsEnd (0x10A028): 1 -- so Update resets the decoder, and a movie that does not loop (+0x4B == 0) completes
//   (+0x4A, then IsCompleted +0x49): the game goes on; a looping one restarts and shows a still picture.
// Both run only where the call would otherwise go to the fallback (no native version exists), so with the stand-in
// off nothing changes. sceMpegCreate / Reset / AddCallback still run retail in the fallback (the IPU stand-in,
// B3NATIVE_IPU_STANDIN=1, answers their register accesses). One line per movie (decoder) and per reset; a summary.
#ifndef B3NATIVE_EXPLORE_STANDINS_H
#define B3NATIVE_EXPLORE_STANDINS_H

#include "b3host/call.h"
#include "b3host/native_backend.h"
#include "hook_table.h"
#include "native_fallback.h"
#include "native_table.h"
#include "platform.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace b3native {

class MovieStandIn {
public:
    static constexpr b3host::gaddr kGetPicture = 0x00109F00u, kIsEnd = 0x0010A028u, kReset = 0x0010A048u;

    static bool requested() {
        const char* e = std::getenv("B3NATIVE_MOVIE_STANDIN");
        return e && e[0] == '1';
    }

    // Wraps the backend's fallback (install the fallback first). Returns false if there is none to wrap.
    bool install(b3host::NativeBackend& nb) {
        if (!nb.fallback) return false;
        inner_ = nb.fallback;
        nb.fallback = [this](b3host::gaddr f, const b3host::CallArgs& a, b3host::CallResult& r) {
            if (f == kGetPicture || f == kIsEnd) { stand_in(f, a, r); return; }
            if (f == kReset) note_reset(a.gpr[0].w[0]);
            inner_(f, a, r);
        };
        std::printf("EXPLORATION: movie stand-in ON (B3NATIVE_MOVIE_STANDIN): every movie ends at once; "
                    "not a proof, not for tick hashes\n");
        std::fflush(stdout);
        return true;
    }

    void report(std::FILE* out) const {
        std::fprintf(out, "EXPLORATION: movie stand-in: %llu movies ended at once (%llu sceMpegGetPicture, %llu sceMpegIsEnd "
                     "answered; %zu decoders)\n", static_cast<unsigned long long>(ended_), static_cast<unsigned long long>(pictures_),
                     static_cast<unsigned long long>(ends_), state_.size());
    }

private:
    void stand_in(b3host::gaddr f, const b3host::CallArgs& a, b3host::CallResult& r) {
        const b3host::gaddr mp = a.gpr[0].w[0];
        r.v0 = {};
        if (f == kIsEnd) {
            ++ends_;
            r.v0.w[0] = 1;
            return;
        }
        ++pictures_;
        Decoder& d = state_[mp];
        if (!d.done) {
            d.done = true;
            ++ended_;
            if (++d.movies <= 3) {   // a looping movie restarts every Update: its first three are named, then counted
                std::printf("EXPLORATION: movie stand-in: movie %llu (decoder %08X, its %u%s) ends at once, at VBlank %llu\n",
                            static_cast<unsigned long long>(ended_), mp, d.movies, d.movies == 3 ? "rd; further ones counted" : "",
                            static_cast<unsigned long long>(b3native::platform::vblank_count()));
                std::fflush(stdout);
            }
        }
    }
    void note_reset(b3host::gaddr mp) {
        auto it = state_.find(mp);
        if (it != state_.end()) it->second.done = false;   // the next picture is a new movie (or a loop's restart)
    }

    std::function<void(b3host::gaddr, const b3host::CallArgs&, b3host::CallResult&)> inner_;
    struct Decoder { bool done = false; unsigned movies = 0; };
    std::map<b3host::gaddr, Decoder> state_;
    std::uint64_t ended_ = 0, pictures_ = 0, ends_ = 0;
};

// Retail-for stand-in (B3NATIVE_EXPLORE_RETAIL=addr[,addr...], native_play --explore-retail): the listed functions run
// as retail code in the fallback instead of natively -- for a native version that is known wrong and whose fix belongs
// to another lane (the decomp, the generator). The hook table (hook_table.h switch_off) gives the listed rows a
// switched-off flag (native_override() then returns null for them; host.h), so nothing outside this run changes.
// Needs the fallback. Logged at install with each function's name; a summary of how often each one ran.
class RetailFor {
public:
    static std::string requested() {
        const char* e = std::getenv("B3NATIVE_EXPLORE_RETAIL");
        return e ? std::string(e) : std::string();
    }

    // `list`: hex addresses separated by commas, switched off on the hook table. Returns false (and says why) if an
    // address has no native entry or there is no fallback.
    bool install(b3host::NativeBackend& nb, NativeFallback& fb, const std::string& list, HookTable& hooks) {
        if (!nb.fallback) { std::fprintf(stderr, "explore-retail: needs the fallback\n"); return false; }
        std::vector<b3host::gaddr> addrs;
        std::size_t from = 0;
        while (from < list.size()) {
            std::size_t to = list.find(',', from);
            if (to == std::string::npos) to = list.size();
            const std::string tok = list.substr(from, to - from);
            from = to + 1;
            if (tok.empty()) continue;
            const auto a = static_cast<b3host::gaddr>(std::stoul(tok, nullptr, 16));
            if (!hooks.has_row(a)) {
                std::fprintf(stderr, "explore-retail: %08X has no native entry\n", a);
                return false;
            }
            addrs.push_back(a);
        }
        std::string error;
        if (addrs.empty() || !hooks.switch_off("port:explore_retail", addrs, &off_, &error)) {
            std::fprintf(stderr, "explore-retail: %s\n", addrs.empty() ? "no address" : error.c_str());
            return false;
        }
        for (b3host::gaddr a : addrs) {
            fb.retail_for(a);   // and the mini runs its retail code instead of calling back to the native version
            ran_[a] = 0;
        }
        inner_ = nb.fallback;
        nb.fallback = [this](b3host::gaddr f, const b3host::CallArgs& a, b3host::CallResult& r) {
            auto it = ran_.find(f);
            if (it != ran_.end()) ++it->second;
            inner_(f, a, r);
        };
        for (const auto& [a, n] : ran_)
            std::printf("EXPLORATION: retail-for stand-in: %08X %s runs as retail code (fallback), not natively; "
                        "not a proof, not for tick hashes\n", a, b3host::describe_function(a).c_str());
        std::fflush(stdout);
        return true;
    }

    void report(std::FILE* out) const {
        for (const auto& [a, n] : ran_)
            std::fprintf(out, "EXPLORATION: retail-for stand-in: %08X ran %llu times as retail code\n", a,
                         static_cast<unsigned long long>(n));
    }

private:
    std::atomic<bool> off_{false};
    std::map<b3host::gaddr, std::uint64_t> ran_;
    std::function<void(b3host::gaddr, const b3host::CallArgs&, b3host::CallResult&)> inner_;
};

// Return stand-in (native_play --explore-return ADDR=V[,ADDR=V...] / B3NATIVE_EXPLORE_RETURN): each listed function,
// where it would run retail in the fallback, returns V (v0) at once without running -- for a call into a part of the
// platform not modelled yet whose effect the exploration does not need (first use: sceDbcSendData2 0x216A58 = 1, the
// pad's vibration send accepted and dropped: DBCMAN's 0x8000131B is not modelled, boot2's). Logged at install with
// each function's name; a summary of how often each one was answered.
class ReturnStandIn {
public:
    static std::string requested() {
        const char* e = std::getenv("B3NATIVE_EXPLORE_RETURN");
        return e ? std::string(e) : std::string();
    }
    bool install(b3host::NativeBackend& nb, const std::string& list) {
        if (!nb.fallback) { std::fprintf(stderr, "explore-return: needs the fallback\n"); return false; }
        std::size_t from = 0;
        while (from < list.size()) {
            std::size_t to = list.find(',', from);
            if (to == std::string::npos) to = list.size();
            const std::string tok = list.substr(from, to - from);
            from = to + 1;
            if (tok.empty()) continue;
            const std::size_t eq = tok.find('=');
            if (eq == std::string::npos) { std::fprintf(stderr, "explore-return: %s is not ADDR=V\n", tok.c_str()); return false; }
            const auto a = static_cast<b3host::gaddr>(std::stoul(tok.substr(0, eq), nullptr, 16));
            entries_[a] = {static_cast<std::uint32_t>(std::stoul(tok.substr(eq + 1), nullptr, 0)), 0};
        }
        inner_ = nb.fallback;
        nb.fallback = [this](b3host::gaddr f, const b3host::CallArgs& a, b3host::CallResult& r) {
            auto it = entries_.find(f);
            if (it == entries_.end()) { inner_(f, a, r); return; }
            ++it->second.calls;
            r.v0 = {};
            r.v0.w[0] = it->second.value;
        };
        for (const auto& [a, e] : entries_)
            std::printf("EXPLORATION: return stand-in: %08X %s returns %u at once (not run); not a proof, not for tick "
                        "hashes\n", a, b3host::describe_function(a).c_str(), e.value);
        std::fflush(stdout);
        return true;
    }
    void report(std::FILE* out) const {
        for (const auto& [a, e] : entries_)
            std::fprintf(out, "EXPLORATION: return stand-in: %08X answered %llu times\n", a,
                         static_cast<unsigned long long>(e.calls));
    }
private:
    struct Entry { std::uint32_t value; std::uint64_t calls; };
    std::map<b3host::gaddr, Entry> entries_;
    std::function<void(b3host::gaddr, const b3host::CallArgs&, b3host::CallResult&)> inner_;
};

}  // namespace b3native

#endif
