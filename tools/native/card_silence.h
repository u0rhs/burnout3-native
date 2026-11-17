// card_silence.h -- save1 (remaster-features §8, 2026-11-09: no memory card): the card is the port's own save
// file (src/native/save_store.h; always present, formatted, roomy), so the game's card progress and confirmation
// messages are not shown and its saves (autosave included) happen silently.
//
// Every card page shows its messages through one message component, D_01E8F360 [C the state setters func_003ECCD0,
// func_003EE470, func_003F4600, func_0041FE00, func_004365B0, func_0042E310 / 0042E6B0]: func_00449F10(component, type,
// GLOBALUS.BIN string id, ...) prepares it (type 0 a progress box without buttons, 1 yes / no, 2 an OK box), and the
// pages read the answer through func_00449150(component, pad, 1) [C 0x449150: 0xD while inactive or unanswered, 2 for
// OK on a type-2 box, 0 / 1 for yes / no]. Two wrappers on the hook table (hook_table.h, port layer):
//   - func_00449F10: a progress box with a card text (progress(): "Checking / Loading / Saving / Formatting memory card
//     (PS2) ... Do not remove ...", "Autosaving", "Saving", "Loading") is not prepared: the page goes on as it does
//     while the box shows (the card controller's completion moves it, not the box). An OK box that only reports a
//     card load or save (auto_ok(): "Load successful", "Save successful", "Autosave successful") is not prepared either,
//     and its OK is given:
//   - func_00449150: answers 2 (OK) once for that box, as a confirm press would.
// "Would you like to save your profile?" (auto_yes(), yes / no after a new profile's name: a PS2 might have had no card
// or no room) is answered YES the same way: the profile is always saved, as the port's card always takes it.
// Questions (yes / no: overwrite, autosave on / off, save your profile?) and the errors stay as the game has them; with
// the port's card the errors cannot occur (no card missing, unformatted, full or corrupt). Menu flow only: nothing a race
// or a save reads is touched. B3NATIVE_CARD_MESSAGES=1 shows every message (retail).
#ifndef B3NATIVE_CARD_SILENCE_H
#define B3NATIVE_CARD_SILENCE_H

#include "b3host/call.h"
#include "b3host/host.h"
#include "hook_table.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace b3native {

class CardSilence {
public:
    // Off with B3NATIVE_CARD_MESSAGES=1.
    static bool requested() {
        const char* e = std::getenv("B3NATIVE_CARD_MESSAGES");
        return !(e && e[0] == '1' && e[1] == 0);
    }
    // On the hook table's port layer (after PcMenu::install).
    bool install(HookTable& hooks);
    std::uint64_t hidden() const { return hidden_; }
    std::uint64_t confirmed() const { return confirmed_; }

    // GLOBALUS.BIN string ids [C DATA/GLOBALUS.BIN: count at +8, u32 offsets from 0x10, UTF-16]
    static bool progress(std::uint32_t id) {
        switch (id) {
        case 120:    // "Checking memory card (PS2) in MEMORY CARD slot 1. Do not remove ..." (network configuration)
        case 168:    // "Checking memory card (PS2) in MEMORY CARD slot 1.  Do not remove ..."
        case 169:    // "Loading save game data from memory card (PS2) ... Do not remove ..."
        case 171:    // "Saving game data to memory card (PS2) ... Do not remove ..."
        case 175:    // "Formatting memory card (PS2) ... Do not remove ..."
        case 2260:   // "Autosaving"
        case 2261:   // "Saving"
        case 2262:   // "Loading"
            return true;
        default:
            return false;
        }
    }
    static bool auto_ok(std::uint32_t id) {
        return id == 205 /* "Load successful" */ || id == 2256 /* "Save successful" */ || id == 2263 /* "Autosave successful" */;
    }
    static bool auto_yes(std::uint32_t id) { return id == 2257; /* "Would you like to save your profile?" */ }

private:
    static CardSilence*& instance() { static CardSilence* p = nullptr; return p; }
    static void show(const b3host::CallArgs& a, b3host::CallResult& r);
    static void answer(const b3host::CallArgs& a, b3host::CallResult& r);

    void (*show_)(const b3host::CallArgs&, b3host::CallResult&) = nullptr;
    void (*answer_)(const b3host::CallArgs&, b3host::CallResult&) = nullptr;
    int pending_answer_ = -1;   // the answer func_00449150 gives next for a box not shown (-1: none)
    std::uint64_t hidden_ = 0, confirmed_ = 0;
};

namespace card_silence_detail {
constexpr b3host::gaddr kMessage = 0x01E8F360u;                   // the message component [C every card page]
constexpr b3host::gaddr kShow = 0x00449F10u, kAnswer = 0x00449150u;
constexpr std::uint32_t kTypeProgress = 0, kTypeYesNo = 1, kTypeOk = 2, kAnswerYes = 0, kAnswerOk = 2;
inline u_long128 reg(std::uint32_t v) { u_long128 q{}; q.w[0] = v; return q; }
}  // namespace card_silence_detail

inline bool CardSilence::install(HookTable& hooks) {
    using namespace card_silence_detail;
    const bool sp = hooks.has_row(kShow), ap = hooks.has_row(kAnswer);
    if (!sp || !ap) {
        std::fprintf(stderr, "card_silence: the message component's functions are not native (%d %d): card messages as retail\n",
                     sp, ap);
        return false;
    }
    std::string error;
    if (!hooks.install("port:card_silence", {{kShow, HookLayer::kPort, &show, &show_}, {kAnswer, HookLayer::kPort, &answer, &answer_}},
                       &error)) {
        std::fprintf(stderr, "card_silence: %s: card messages as retail\n", error.c_str());
        return false;
    }
    instance() = this;
    std::printf("card_silence: memory card progress and save-confirmation messages hidden (B3NATIVE_CARD_MESSAGES=1: shown)\n");
    return true;
}

// func_00449F10(component, type, string id, buttons, ...)
inline void CardSilence::show(const b3host::CallArgs& a, b3host::CallResult& r) {
    using namespace card_silence_detail;
    CardSilence* p = instance();
    const b3host::gaddr self = a.gpr[0].w[0] & 0x01FFFFFFu;
    const std::uint32_t type = a.gpr[1].w[0], id = a.gpr[2].w[0];
    if (self == kMessage) {
        const bool hide_progress = type == kTypeProgress && progress(id);
        const bool hide_ok = type == kTypeOk && auto_ok(id);
        const bool hide_yes = type == kTypeYesNo && auto_yes(id);
        if (hide_progress || hide_ok || hide_yes) {
            p->pending_answer_ = hide_ok ? int(kAnswerOk) : hide_yes ? int(kAnswerYes) : -1;
            ++p->hidden_;
            std::printf("card_silence: message %u (type %u) not shown%s\n", id, type,
                        hide_ok ? ", answered OK" : hide_yes ? ", answered YES" : "");
            std::fflush(stdout);
            r.v0 = reg(0);
            return;
        }
        p->pending_answer_ = -1;
        std::printf("card_silence: message %u (type %u) shown\n", id, type);   // the game's own (tools/native/save_check.sh reads it)
        std::fflush(stdout);
    }
    p->show_(a, r);
}

// func_00449150(component, pad, 1): the answer of a box not shown (OK / YES), once
inline void CardSilence::answer(const b3host::CallArgs& a, b3host::CallResult& r) {
    using namespace card_silence_detail;
    CardSilence* p = instance();
    if ((a.gpr[0].w[0] & 0x01FFFFFFu) == kMessage && p->pending_answer_ >= 0) {
        r.v0 = reg(static_cast<std::uint32_t>(p->pending_answer_));
        p->pending_answer_ = -1;
        ++p->confirmed_;
        return;
    }
    p->answer_(a, r);
}

}  // namespace b3native

#endif
