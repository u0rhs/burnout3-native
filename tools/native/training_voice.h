// training_voice.h -- the race training's narration in the player's language (1.3.0, djlang; docs/pc-settings.md §7.19).
//
// The race training (DRIVER DETAILS > TRAINING, and the World Tour's first events) plays two movies: a picture
// (OVID/TRC_<lang>.M2V or TRD_<lang>.M2V) with its narration from OVID/MOVIE.RWS, which holds every language's
// (e_ f_ g_ i_ j_ s_train segments; the same file on the US and the European discs). The two training states
// (func_0043FFB0, func_0043EFA0 [C decomp]) pick the movie index D_01E90208 from the language word 0x004E2918 and start
// it with func_00424750(page, movie, flag, help text, duration); the movie player opens D_004B5300[movie]'s picture and
// MOVIE.RWS at segment D_004B5540[movie] (D_004B5300: 10..17 the TrC_ pictures, 18..25 the TrD_). The US disc keeps the
// word at 1: the English movies 0xB (TrC_ENG, MOVIE.RWS segment 5, 98.5 s) and 0x13 (TrD_ENG, segment 4, 123.7 s).
//
// The word stays 1 (it also sets number formats and the profile's language byte: speed_units.h). Instead this wraps
// func_00424750 on the hook table's port layer: a call with an English training movie (TrC 0xB / TrD 0x13) starts the
// same training movie of the chosen language (TrC / TrD: FR 0xC / 0x14, GE 0xD / 0x15, IT 0xE / 0x16, SP 0x10 / 0x18), as the two states
// would with that language's word; the help text and duration they pass are the same for every language, and each
// language's narration segments are the same size as the English ones (TrD 4521984, TrC 3604480 bytes), so the voice and the
// picture keep their length. The localized picture is the player's import (b3asset/language.h Pack::kTraining), else
// the English picture (src/native/platform_disc.cpp set_languages_root). Installed only for French / German / Italian /
// Spanish with VOICES Language: with English nothing is wrapped and the game runs as it always did.
#ifndef B3NATIVE_TRAINING_VOICE_H
#define B3NATIVE_TRAINING_VOICE_H

#include "hook_table.h"
#include "platform.h"

#include "b3host/host.h"

#include <cstdint>
#include <cstdio>
#include <string>

namespace b3native {

class TrainingVoice {
public:
    static constexpr b3host::gaddr kStartMovie = 0x00424750u;   // func_00424750(page, movie, flag, help text, duration)
    static constexpr std::uint32_t kEnglishTrC = 0xB, kEnglishTrD = 0x13;
    // The language's two training movies (TrC, TrD) for a disc code; false for a language without its own narration
    static bool movies(const std::string& code, std::uint32_t& trc, std::uint32_t& trd) {
        if (code == "FR") trc = 0xC, trd = 0x14;
        else if (code == "GE") trc = 0xD, trd = 0x15;
        else if (code == "IT") trc = 0xE, trd = 0x16;
        else if (code == "SP") trc = 0x10, trd = 0x18;
        else return false;
        return true;
    }
    // The movie the wrapped call starts instead of `movie`
    std::uint32_t map(std::uint32_t movie) const { return movie == kEnglishTrC ? trc_ : movie == kEnglishTrD ? trd_ : movie; }

    bool install(HookTable& hooks, const std::string& code) {
        if (!movies(code, trc_, trd_)) return false;
        if (!hooks.has_row(kStartMovie)) {
            std::fprintf(stderr, "training_voice: %08X is not native: the English narration\n", kStartMovie);
            return false;
        }
        instance() = this;
        if (!hooks.install("port:training_voice", {{kStartMovie, HookLayer::kPort, &call, &next_}})) {
            instance() = nullptr;
            return false;
        }
        code_ = code;
        std::printf("training_voice: the race training's narration in %s (TrC 0x%X for 0x%X, TrD 0x%X for 0x%X)\n", code.c_str(), trc_,
                    kEnglishTrC, trd_, kEnglishTrD);
        return true;
    }

private:
    static TrainingVoice*& instance() { static TrainingVoice* p = nullptr; return p; }
    static inline HookEntry next_ = nullptr;
    static void call(const b3host::CallArgs& a, b3host::CallResult& r) {
        const TrainingVoice* p = instance();
        const std::uint32_t movie = a.gpr[1].w[0];
        const std::uint32_t want = p ? p->map(movie) : movie;
        if (want == movie) return next_(a, r);
        b3host::CallArgs b = a;
        b.gpr[1].w[0] = want;
        b.gpr[1].w[1] = 0;
        std::printf("training_voice: VBlank %llu: training movie 0x%X -> 0x%X (%s)\n", static_cast<unsigned long long>(platform::vblank_count()),
                    movie, want, p->code_.c_str());
        std::fflush(stdout);
        next_(b, r);
    }
    std::uint32_t trc_ = 0, trd_ = 0;
    std::string code_;
};

}  // namespace b3native

#endif
