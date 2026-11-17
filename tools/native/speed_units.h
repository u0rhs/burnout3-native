// speed_units.h -- the speed units setting of native_play (units1, 2026-11-14): the game's speeds and distances in
// miles (the US game's) or kilometres, SETTINGS > INTERFACE > SPEED UNITS.
//
// How the game chooses [C decomp, every reader of D_004E2918 in gen/native]: the word at 0x004E2918 is the game's
// language. func_00133190 sets 1 at the boot (and func_0021B770 sets 1 again), the only value the US disc ever has;
// func_00437060's language page, which the US disc never reaches, sets 0, 10 or 5. The word also picks the number
// separators (func_00131B30), the '$' place (func_003B8840), the online presence strings (D_004D1B70[lang]), the
// sprintf formats (D_004A55D0[lang]), FSM ids (func_0043EFA0, func_0043FFB0) and the language byte a profile saves
// (func_0039FA70, func_0039FD90, func_003A0050), so changing it for good would change more than units. Its readers
// that choose units all test "> 1" (">= 2" in UpdateSmallMessage): 0 and 1 (US / UK English) mean miles, every other
// language kilometres. With SPEED UNITS at KM/H each function listed in kSites runs with the word at 2 and the word
// goes back to the game's value when the call returns. Value 2 takes the default arm of every other switch in those
// functions and in their callees (3, 4 and 5 add a space before the unit; func_003B8840 puts '$' first for 2 as for
// 1; func_00131B30 is not reached), so only the unit and the number change. Presentation only: the m/s-to-mph factor
// the sound, AI, score logic, camera and crash analyser use (D_004E0798) never reads the word. Checked against an MPH
// run (docs/pc-settings.md §7.16): the speed equal on every VBlank; guest RAM different only in the HUD's text and
// layout, the draw packets, and the clock-derived words (hi-res timer, audio voice stamps) the extra glyphs' virtual
// EE time moves.
//
// The speedometer's label is written once, at its Prepare (race start); its Update follows the setting latched there,
// so a change made during a race shows on the speedometer from the next race. Every other site uses the setting at
// the moment it draws. With the default (MPH) the hooks store nothing: the game runs as it always did.
// B3NATIVE_SPEED_UNITS=0 / 1 overrides the setting (tests); B3NATIVE_SPEED_LOG=1 logs the speedometer's number each
// VBlank it draws (with the speed in m/s it shows).
#ifndef B3NATIVE_SPEED_UNITS_H
#define B3NATIVE_SPEED_UNITS_H

#include "hook_table.h"

#include "b3host/call.h"
#include "b3host/host.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <string>
#include <vector>

namespace b3native {

class SpeedUnits {
public:
    using UnitsFn = int (*)();              // 0 mph (the game's), 1 km/h -- read at every call
    using VblankFn = std::uint64_t (*)();   // the VBlank count, for B3NATIVE_SPEED_LOG (null: no log)
    static constexpr b3host::gaddr kLanguage = 0x004E2918u;
    static constexpr std::uint32_t kMetricLanguage = 2;
    static constexpr b3host::gaddr kSpeedoPrepare = 0x001A3C70u, kSpeedoUpdate = 0x001A3B30u, kRecordsPage = 0x00376F60u;
    struct Site {
        b3host::gaddr address;
        const char* what;
    };
    static constexpr Site kSites[] = {
        {kSpeedoPrepare, "CB3HUDSpeedo::Prepare (the speedometer's label, 2007 kph / 2008 mph)"},
        {kSpeedoUpdate, "CB3HUDSpeedo::Update (the speedometer's number)"},
        {0x001A2700u, "CB3HUDScore::UpdateSmallMessage (the distance messages, 2030 KM / 2031 MILES)"},
        {0x003CAAB0u, "the race score list's distances (km / mi)"},
        {0x003D8920u, "the escape distance (km / mi)"},
        {0x00364300u, "an item's value (m / ft)"},
        {0x003B8670u, "a distance text (m / ft)"},
        {0x00376880u, "a records row (M / FT)"},
        {kRecordsPage, "the records page (M / FT)"},
        {0x0043A420u, "a stat's distance (M / FT)"},
        {0x003F1490u, "Driver Details stats 21 / 24 (M / FT)"},
    };
    static constexpr std::size_t kSiteCount = std::size(kSites);

    // On the hook table's port layer, all the sites or none (then the game's units)
    bool install(HookTable& hooks, UnitsFn units, VblankFn vblank = nullptr) {
        for (const Site& s : kSites)
            if (!hooks.has_row(s.address)) {
                std::fprintf(stderr, "speed_units: %08X (%s) is not native: the game's units\n", s.address, s.what);
                return false;
            }
        slots_.clear();
        slots_.reserve(kSiteCount);   // the bound hooks keep a pointer to their slot
        std::vector<Hook> set;
        for (const Site& s : kSites) {
            slots_.push_back({s.address, this});
            set.push_back(bind_hook(s.address, HookLayer::kPort, &call, &slots_.back()));
        }
        std::string error;
        if (!hooks.install("port:speed_units", set, &error)) {
            std::fprintf(stderr, "speed_units: %s: the game's units\n", error.c_str());
            slots_.clear();
            return false;
        }
        units_ = units;
        env_ = -1;
        if (const char* e = std::getenv("B3NATIVE_SPEED_UNITS"); e && (e[0] == '0' || e[0] == '1') && !e[1]) env_ = e[0] - '0';
        const char* l = std::getenv("B3NATIVE_SPEED_LOG");
        vblank_ = l && *l && std::strcmp(l, "0") != 0 ? vblank : nullptr;
        std::printf("speed_units: the game's speeds and distances follow SETTINGS > INTERFACE > SPEED UNITS%s\n",
                    env_ < 0 ? "" : env_ ? " (B3NATIVE_SPEED_UNITS=1: KM/H)" : " (B3NATIVE_SPEED_UNITS=0: MPH)");
        return true;
    }
    // 0 MPH, 1 KM/H: what the sites use now (the environment wins)
    int units() const {
        if (env_ >= 0) return env_;
        return units_ ? (units_() == 1 ? 1 : 0) : 0;
    }

private:
    struct Slot {
        b3host::gaddr address;
        SpeedUnits* self;
    };
    static void call(const b3host::CallArgs& a, b3host::CallResult& r, void* user, HookEntry next) {
        const Slot& s = *static_cast<const Slot*>(user);
        SpeedUnits& p = *s.self;
        int want = p.units();
        if (s.address == kSpeedoPrepare) p.speedo_units_ = want;
        else if (s.address == kSpeedoUpdate && p.speedo_units_ >= 0) want = p.speedo_units_;
        // MPH: nothing stored. Above 1 already (a nested site, or a language that writes kilometres): the game's own
        const std::uint32_t game = want == 1 ? b3host::load<std::uint32_t>(kLanguage) : 0u;
        const bool swap = want == 1 && game <= 1;
        if (swap) b3host::store<std::uint32_t>(kLanguage, kMetricLanguage);
        next(a, r);
        if (swap) b3host::store<std::uint32_t>(kLanguage, game);
        if (p.vblank_ && s.address == kSpeedoUpdate) p.log_speedo(a.gpr[0].w[0], want);
    }
    // the speedometer's text: UTF-16 at self + 0x30 (8 units); its source: self + 0x2C -> +0xB0, the W lane (m/s)
    void log_speedo(b3host::gaddr self, int want) {
        const std::uint64_t v = vblank_();
        if (v == logged_) return;   // a second player's speedometer on the same VBlank: the first one's line only
        logged_ = v;
        std::string t;
        for (b3host::gaddr k = 0; k < 16; k += 2) {
            const std::uint16_t c = b3host::load<std::uint16_t>(self + 0x30u + k);
            if (!c) break;
            t += c < 0x80 ? static_cast<char>(c) : '?';
        }
        const b3host::gaddr src = b3host::load<std::uint32_t>(self + 0x2Cu);
        float ms = 0;
        if (src) {
            const std::uint32_t w = b3host::load<std::uint32_t>(src + 0xB0u + 0xCu);
            std::memcpy(&ms, &w, sizeof ms);
        }
        std::printf("speed_units: VBlank %llu: speedometer %s %s (%.3f m/s)\n", static_cast<unsigned long long>(v), t.c_str(),
                    want == 1 ? "kph" : "mph", static_cast<double>(ms));
    }
    std::vector<Slot> slots_;
    UnitsFn units_ = nullptr;
    VblankFn vblank_ = nullptr;
    int env_ = -1;            // B3NATIVE_SPEED_UNITS (-1: not set)
    int speedo_units_ = -1;   // the setting at the speedometer's last Prepare (-1: none yet)
    std::uint64_t logged_ = ~0ull;
};

}  // namespace b3native

#endif
