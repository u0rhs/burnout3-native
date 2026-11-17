// online_message.h -- the port's own words in the ONLINE dialog.
//
// The native game has no network: the port answers SMAP.IRX's load with -1 (no network adapter,
// src/native/platform_sif.cpp), so picking ONLINE on the main menu shows a message box whose text is the PS2's "no
// network adaptor" message: GLOBALUS.BIN string 105 (0x69), the id CB3NetworkManager::GetAdapterErrorString 0x26D1E0
// gives for adapter error 2; string 107 (0x6B, error 8) carries the same text [D GLOBALUS.BIN; C decomp
// B3NetworkManager_0026D1E0.cpp; callers B3OnlineOpenConnectionState 0x346200, func_00345D00,
// B3OnlineRetrieveConfigsPage 0x349E00: CB3MessageBox::Prepare(85, <that id>, ...)]. The message box asks the string
// lookup func_00271A00 (CB3Strings::GetString) for the id when it draws.
//
// This wrapper on the lookup answers those two ids with the port's text (kText below, UTF-16 in free guest RAM: the mod
// arena's port region GuestArena::kOnlineMessage, checked zero before the first write, as date_format.h does). Same
// dialog, same OK button, same flow: only the words. Every other id is the game's, untouched.
//
// A text mod wins: the wrapper first runs the layers below it (the generated lookup, a mod's replacement) and swaps the
// text only when what they return is the disc's own message, recognised by its length and FNV-1a 64 hash (kRetail*;
// the game's words are not kept here). A GLOBALUS.BIN from a mod that rewrites string 105 / 107 therefore shows the
// mod's text; a mod's wrap of the lookup sits above this layer anyway (hook_table.h). B3NATIVE_ONLINE_MESSAGE=0 leaves
// the game's text (comparisons).
//
// 1.1: with a language from the player's European disc (set_language: native_play passes platform_disc.h
// language_text(), empty when a mod's table is over it) the lookup's text is that language's own no-adapter message;
// the port's words then come in that language (text_for).
#ifndef B3NATIVE_ONLINE_MESSAGE_H
#define B3NATIVE_ONLINE_MESSAGE_H

#include "hook_table.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace b3native {

class OnlineMessage {
public:
    // On the hook table's port layer; false (and the game's text) when the lookup is not native or the env says 0.
    bool install(HookTable& hooks);
    // 1.1: the code of the language pack the game's text comes from (empty: the disc's own or a mod's)
    void set_language(std::string code) { language_ = std::move(code); }
    ~OnlineMessage() {
        if (instance() == this) instance() = nullptr;
    }

    static constexpr b3host::gaddr kLookup = 0x00271A00u;   // func_00271A00(string table, id) -> UTF-16 text
    static constexpr std::uint32_t kNoAdapter = 0x69u;      // 105: GetAdapterErrorString(2), the one the port shows
    static constexpr std::uint32_t kNoAdapter2 = 0x6Bu;     // 107: GetAdapterErrorString(8), the same words
    // The port's words (ASCII: every character is in the game's fonts); one sentence, one line in the box.
    static constexpr char kText[] = "Online play is coming soon.";
    // The disc's (NTSC-U) text of both ids: 94 UTF-16 units, FNV-1a 64 over its UTF-16LE bytes
    static constexpr std::uint32_t kRetailUnits = 94;
    static constexpr std::uint64_t kRetailHash = 0x1A23FB1438B9EE25ull;
    static constexpr b3host::gaddr kAt = GuestArena::kOnlineMessage.address;
    static_assert(kAt == 0x000C1200u && GuestArena::kOnlineMessage.size == 0x100u, "the online message stays where it is");
    static_assert(2 * sizeof kText <= GuestArena::kOnlineMessage.size, "the text fits its region");

    // 1.1: the port's words in a language's code (UTF-8; the game's fonts have the European letters); kText otherwise
    static const char* text_for(const std::string& code) {
        static const struct { const char* code; const char* text; } kTexts[] = {
            {"FR", "Le jeu en ligne arrive bient\xC3\xB4t."},
            {"GE", "Der Online-Modus kommt bald."},
            {"IT", "Il gioco online arriver\xC3\xA0 presto."},
            {"SP", "El juego online llegar\xC3\xA1 pronto."},
            {"DU", "Online spelen komt binnenkort."},
            {"SW", "Onlinespel kommer snart."},
        };
        for (const auto& t : kTexts)
            if (code == t.code) return t.text;
        return kText;
    }
    static bool is_message(std::uint32_t id) { return id == kNoAdapter || id == kNoAdapter2; }
    // FNV-1a 64 over the UTF-16LE bytes of the guest string at `at`; *units its length (capped at 1024 units)
    static std::uint64_t guest_hash(b3host::gaddr at, std::uint32_t* units);
    static bool is_retail(b3host::gaddr at) {
        std::uint32_t n = 0;
        return at != 0 && guest_hash(at, &n) == kRetailHash && n == kRetailUnits;
    }

private:
    static OnlineMessage*& instance() { static OnlineMessage* p = nullptr; return p; }
    static void lookup(const b3host::CallArgs& a, b3host::CallResult& r);
    bool write_text();   // kText at kAt, once; false: the region was in use

    void (*lookup_)(const b3host::CallArgs&, b3host::CallResult&) = nullptr;
    std::string language_;
    bool written_ = false;
    bool disabled_ = false;
    bool logged_ = false;
};

inline bool OnlineMessage::install(HookTable& hooks) {
    if (const char* e = std::getenv("B3NATIVE_ONLINE_MESSAGE"); e && e[0] == '0' && !e[1]) {
        std::printf("online_message: off (B3NATIVE_ONLINE_MESSAGE=0): the game's no-adapter text\n");
        return false;
    }
    if (!hooks.has_row(kLookup)) {
        std::fprintf(stderr, "online_message: the string lookup is not native: the game's no-adapter text\n");
        return false;
    }
    std::string error;
    if (!hooks.install("port:online_message", {{kLookup, HookLayer::kPort, &lookup, &lookup_}}, &error)) {
        std::fprintf(stderr, "online_message: %s: the game's no-adapter text\n", error.c_str());
        return false;
    }
    instance() = this;
    return true;
}

inline std::uint64_t OnlineMessage::guest_hash(b3host::gaddr at, std::uint32_t* units) {
    std::uint64_t h = 0xCBF29CE484222325ull;
    std::uint32_t n = 0;
    for (; n < 1024; ++n) {
        const std::uint16_t c = b3host::load<std::uint16_t>(at + 2 * n);
        if (c == 0) break;
        h = (h ^ (c & 0xFFu)) * 0x100000001B3ull;
        h = (h ^ (c >> 8)) * 0x100000001B3ull;
    }
    if (units) *units = n;
    return h;
}

inline bool OnlineMessage::write_text() {
    if (written_) return true;
    if (disabled_) return false;
    for (b3host::gaddr k = 0; k < GuestArena::kOnlineMessage.size; k += 4)
        if (b3host::load<std::uint32_t>(kAt + k) != 0) {
            std::fprintf(stderr, "online_message: guest RAM %08X is in use: the game's no-adapter text\n", kAt + k);
            disabled_ = true;
            return false;
        }
    b3host::gaddr at = kAt;
    const std::string text = text_for(language_);
    for (std::size_t i = 0; i < text.size() && at + 2 < kAt + GuestArena::kOnlineMessage.size; at += 2) {   // UTF-8 -> UTF-16
        const unsigned char c = static_cast<unsigned char>(text[i]);
        std::uint16_t u = c;
        if (c >= 0xC0 && c < 0xE0 && i + 1 < text.size()) u = static_cast<std::uint16_t>((c & 0x1F) << 6 | (static_cast<unsigned char>(text[i + 1]) & 0x3F)), i += 2;
        else ++i;
        b3host::store<std::uint16_t>(at, u);
    }
    b3host::store<std::uint16_t>(at, 0);
    written_ = true;
    return true;
}

inline void OnlineMessage::lookup(const b3host::CallArgs& a, b3host::CallResult& r) {
    OnlineMessage* p = instance();
    p->lookup_(a, r);   // the layers below first (the game's table, or a mod's)
    // the disc's own words, or (1.1) a language pack's (any text: its words differ per language)
    const bool ours = is_retail(r.v0.w[0]) || (r.v0.w[0] != 0 && !p->language_.empty());
    if (!is_message(a.gpr[1].w[0]) || !ours || !p->write_text()) return;
    if (!p->logged_) {
        p->logged_ = true;
        std::printf("online_message: string %u (no network adapter) answered with the port's text\n", a.gpr[1].w[0]);
    }
    r.v0 = u_long128{};
    r.v0.w[0] = kAt;
}

}  // namespace b3native

#endif
