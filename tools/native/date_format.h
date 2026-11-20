// date_format.h -- the date format setting of native_play (mac5, 2026-11-09: the date shown
// either the US way or as DD/MM/YYYY). The game writes its dates (the profile's DATE OF SAVE, func_004482A0)
// with GLOBALUS.BIN string 2598 (0xA26) "%1/%2/%3", filled with the month, the day and the year [C decomp
// src/nonmatching/func_004482A0.cpp: func_00449FC0(buffer, func_00271A00(&D_0051BA44, 0xA26), &month, &day, &year)].
// With SETTINGS > GAME > LANGUAGE AND UNITS > DATE FORMAT at Day/Month/Year, the string lookup func_00271A00 answers that id with our
// own "%2/%1/%3" (the same fields, day first) in free guest RAM (the mod arena's port region GuestArena::kDateFormat);
// every other string is the game's. Text only: the values the game keeps are untouched. B3NATIVE_DATE_FORMAT=0 / 1 overrides the setting (tests).
#ifndef B3NATIVE_DATE_FORMAT_H
#define B3NATIVE_DATE_FORMAT_H

#include "hook_table.h"
#include "platform.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace b3native {

class DateFormat {
public:
    // format(): 0 month/day/year (the game's), 1 day/month/year -- read at every lookup (the page changes it live)
    using FormatFn = int (*)();
    // On the hook table's port layer (after pc_menu and card_silence)
    bool install(HookTable& hooks, FormatFn format);
    static constexpr std::uint32_t kDateString = 0xA26u;   // "%1/%2/%3"
    static constexpr b3host::gaddr kLookup = 0x00271A00u;  // func_00271A00(string table, id) -> UTF-16 text
    // free guest RAM below the ELF: the mod arena's port region past pc_menu.h's (0xC0000..0xC0400, which it checks is
    // unused before it writes its rows: a string there made the main menu fall back to the game's five rows)
    static constexpr b3host::gaddr kDayFirst = GuestArena::kDateFormat.address;
    static_assert(kDayFirst == 0x000C0800u && GuestArena::kDateFormat.size >= 0x20u, "the date string stays where it was");
    static DateFormat*& instance() { static DateFormat* p = nullptr; return p; }

private:
    static void lookup(const b3host::CallArgs& a, b3host::CallResult& r);
    int format() const {
        if (const char* e = std::getenv("B3NATIVE_DATE_FORMAT"); e && (e[0] == '0' || e[0] == '1') && !e[1]) return e[0] - '0';
        return format_ ? format_() : 0;
    }
    void (*lookup_)(const b3host::CallArgs&, b3host::CallResult&) = nullptr;
    FormatFn format_ = nullptr;
    bool written_ = false;
    bool disabled_ = false;   // the RAM it writes to was in use
};

inline bool DateFormat::install(HookTable& hooks, FormatFn format) {
    if (!hooks.has_row(kLookup)) {
        std::fprintf(stderr, "date_format: the string lookup is not native: dates as the game writes them\n");
        return false;
    }
    std::string error;
    if (!hooks.install("port:date_format", {{kLookup, HookLayer::kPort, &lookup, &lookup_}}, &error)) {
        std::fprintf(stderr, "date_format: %s: dates as the game writes them\n", error.c_str());
        return false;
    }
    format_ = format;
    instance() = this;
    std::printf("date_format: the game's dates follow SETTINGS > GAME > LANGUAGE AND UNITS > DATE FORMAT\n");
    return true;
}

inline void DateFormat::lookup(const b3host::CallArgs& a, b3host::CallResult& r) {
    DateFormat* p = instance();
    // debugging: B3NATIVE_STRING_LOG=<id>[,<id>...] (hex or decimal) logs each lookup of those GLOBALUS.BIN strings
    static const std::vector<std::uint32_t> watch = [] {
        std::vector<std::uint32_t> w;
        if (const char* e = std::getenv("B3NATIVE_STRING_LOG"))
            for (const char* c = e; *c;) {
                char* end = nullptr;
                const unsigned long v = std::strtoul(c, &end, 0);
                if (end == c) break;
                w.push_back(static_cast<std::uint32_t>(v));
                c = *end == ',' ? end + 1 : end;
            }
        return w;
    }();
    if (!watch.empty() && std::find(watch.begin(), watch.end(), a.gpr[1].w[0]) != watch.end()) {
        static std::uint64_t n = 0;
        if (++n <= 50) std::printf("string_log: VBlank %llu: string %u (0x%X) looked up\n", static_cast<unsigned long long>(platform::vblank_count()), a.gpr[1].w[0], a.gpr[1].w[0]);
    }
    if (a.gpr[1].w[0] == kDateString && !p->disabled_ && p->format() == 1) {
        if (!p->written_) {   // "%2/%1/%3" in UTF-16, once, where nothing else is
            for (b3host::gaddr k = 0; k < GuestArena::kDateFormat.size; k += 4)
                if (b3host::load<std::uint32_t>(kDayFirst + k) != 0) {
                    std::fprintf(stderr, "date_format: guest RAM %08X is in use: the game's month/day/year\n", kDayFirst + k);
                    p->disabled_ = true;
                    p->lookup_(a, r);
                    return;
                }
            const char* t = "%2/%1/%3";
            b3host::gaddr at = kDayFirst;
            for (const char* c = t; *c; ++c, at += 2) b3host::store<std::uint16_t>(at, static_cast<std::uint8_t>(*c));
            b3host::store<std::uint16_t>(at, 0);
            p->written_ = true;
        }
        r.v0 = u_long128{};
        r.v0.w[0] = kDayFirst;
        return;
    }
    p->lookup_(a, r);
}

}  // namespace b3native

#endif
