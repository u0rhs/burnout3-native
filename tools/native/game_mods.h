// game_mods.h -- options3 (2026-11-21): PC SETTINGS rows that change what the game itself does (hooks on the hook table's
// port layer). A bitmask from the renderer's settings (b3r_game_mods), read by native_play each VBlank and latched into a
// replay ("game_mods": the game's RAM follows it, so a replay plays with the recording's). Bit 0 is not used (CRASH CAMERA
// is drawn only: src/b3replay/crashhold.h).
//
// DEFORMATION (bits 1-3: 0 the game's, 1 none, 2 50 %, 3 150 %, 4 200 %): CB3VehicleDeform::Impact 0x3DB450 [C its draft:
// f12 the impact size, scaled by the vehicle's stiffness class, clamped to 0..1 at x 0.0125, the excess to AddResidual,
// then ImpactBody bends the struck virtual bones] gets the size times the scale. GAMEPLAY-CHANGING: Impact draws on the
// game's random numbers above a size (the lower panels' random damage), so another size changes the random sequence and
// with it the race from the first hard hit. The scale is latched per race at its start (CB3TrafficSystem::ResetMemberVariables
// 0x1632C0, run as a race is prepared), only in the races the GAMEPLAY options take (game_option_rules.h
// game_options_stage: Single Event's one- and two-player stage records; World Tour, whose results and medals count, and
// online, which must not desync, keep the game's 100 %), and cleared as soon as the game's current mode is no longer
// such a stage (the stage released). A race with one keeps no game records (records_guard.h), does not count for the
// profile (0x136F30) and its ghosts and best times go to their own folder (record_word: bits 32-34).
#ifndef B3NATIVE_GAME_MODS_H
#define B3NATIVE_GAME_MODS_H

#include "b3host/call.h"
#include "b3host/host.h"
#include "hook_table.h"
#include "game_option_rules.h"   // game_options_stage: the races the GAMEPLAY options take
#include "records_guard.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

namespace b3native {

class GameMods {
public:
    enum : std::uint32_t { kDeformShift = 1u, kDeformMask = 7u << 1 };
    static constexpr b3host::gaddr kDeformImpact = 0x003DB450u;      // CB3VehicleDeform::Impact(..., f12 the impact size, ...)
    static constexpr b3host::gaddr kRaceReset = 0x001632C0u;         // CB3TrafficSystem::ResetMemberVariables (a race's start)
    static constexpr b3host::gaddr kProfileCounts = 0x00136F30u;     // whether the race counts for the profile
    // DEFORMATION's setting (0 the game's) -> the impact size's scale
    static float deform_scale(std::uint32_t deformation) {
        static const float kScale[5] = {1.0f, 0.0f, 0.5f, 1.5f, 2.0f};
        return deformation < 5 ? kScale[deformation] : 1.0f;
    }

    // the mask now (native_play: the live settings' or a replay's)
    void set_mask(std::function<std::uint32_t()> f) { mask_ = std::move(f); }
    bool install(HookTable& hooks) {
        for (const b3host::gaddr a : {kDeformImpact, kRaceReset, kRecordsUpdate, kProfileCounts})
            if (!hooks.has_row(a)) {
                std::fprintf(stderr, "game_mods: %08X is not native: DEFORMATION left as the game's\n", a);
                return false;
            }
        const std::vector<Hook> set = {{kDeformImpact, HookLayer::kPort, &deform_impact, &deform_impact_},
                                       {kRaceReset, HookLayer::kPort, &race_reset, &race_reset_},
                                       {kRecordsUpdate, HookLayer::kPort, &records, &records_},
                                       {kProfileCounts, HookLayer::kPort, &profile_counts, &profile_counts_}};
        std::string error;
        if (!hooks.install("port:game_mods", set, &error)) {
            std::fprintf(stderr, "game_mods: %s\n", error.c_str());
            return false;
        }
        instance() = this;
        return true;
    }
    // The race's DEFORMATION (0 the game's), latched at its start; 0 once its stage is released
    std::uint32_t race_deformation() { return deformation_now(); }
    // The race's record word with DEFORMATION in bits 32-34 (records.h options_folder; the flag, TRAFFIC Normal when nothing
    // else set it)
    static constexpr std::uint64_t kRecordMask = 0x7ull << 32;
    static std::uint64_t record_word(std::uint64_t w, std::uint32_t deformation) {
        if (deformation == 0 || deformation > 4) return w;
        return (w ? w : 0x8002u) | std::uint64_t(deformation) << 32;
    }

private:
    using Entry = void (*)(const b3host::CallArgs&, b3host::CallResult&);
    static GameMods*& instance() { static GameMods* p = nullptr; return p; }
    std::uint32_t mask() const { return mask_ ? mask_() : 0u; }
    // a Single Event one- or two-player stage is the game's current mode (game_option_rules.h)
    static bool in_stage() {
        const std::uint32_t cur = b3host::load<std::uint32_t>(kGoCurrentMode) & 0x01FFFFFFu;
        return (cur == kGoOnePlayerStage || cur == kGoTwoPlayerStage) &&
               game_options_stage(cur, b3host::load<std::uint32_t>(cur + 0x1B8u) & 0x01FFFFFFu);
    }
    // the latch, cleared when its stage is gone
    std::uint32_t deformation_now() {
        if (race_deform_ != 0 && !in_stage()) {
            race_deform_ = 0;
            std::printf("game_mods: the stage released: DEFORMATION back to the game's\n");
            std::fflush(stdout);
        }
        return race_deform_;
    }
    // CB3TrafficSystem::ResetMemberVariables: a race starts, its DEFORMATION latched
    static void race_reset(const b3host::CallArgs& a, b3host::CallResult& r) {
        GameMods* p = instance();
        p->race_reset_(a, r);
        const std::uint32_t d = in_stage() ? (p->mask() & kDeformMask) >> kDeformShift : 0u;
        p->race_deform_ = d <= 4 ? d : 0;
        if (p->race_deform_ != 0) {
            std::printf("game_mods: a race with DEFORMATION x%.2f (its records kept apart)\n", double(deform_scale(p->race_deform_)));
            std::fflush(stdout);
        }
    }
    // CB3Records::Update: no record kinds in a race with DEFORMATION
    static void records(const b3host::CallArgs& a, b3host::CallResult& r) {
        GameMods* p = instance();
        if (p->deformation_now() == 0) { p->records_(a, r); return; }
        const std::uint64_t kinds = records_without_kinds(p->records_, a, r);
        std::printf("game_mods: a race with DEFORMATION: the record kinds %llu not written\n", static_cast<unsigned long long>(kinds));
        std::fflush(stdout);
    }
    // 0x136F30: a race with DEFORMATION does not count for the profile
    static void profile_counts(const b3host::CallArgs& a, b3host::CallResult& r) {
        GameMods* p = instance();
        p->profile_counts_(a, r);
        if (p->deformation_now() != 0) r.v0 = u_long128{};
    }
    // CB3VehicleDeform::Impact: the impact size (f12) times the race's DEFORMATION scale
    static void deform_impact(const b3host::CallArgs& a, b3host::CallResult& r) {
        GameMods* p = instance();
        const float k = deform_scale(p->deformation_now());
        if (k == 1.0f) { p->deform_impact_(a, r); return; }
        b3host::CallArgs b = a;
        float size;
        std::memcpy(&size, &a.fpr[0], 4);
        size *= k;
        std::memcpy(&b.fpr[0], &size, 4);
        if (p->scaled_++ == 0) {
            std::printf("game_mods: DEFORMATION x%.2f: an impact's size scaled\n", double(k));
            std::fflush(stdout);
        }
        p->deform_impact_(b, r);
    }
    Entry deform_impact_ = nullptr, race_reset_ = nullptr, records_ = nullptr, profile_counts_ = nullptr;
    std::uint64_t scaled_ = 0;
    std::uint32_t race_deform_ = 0;
    std::function<std::uint32_t()> mask_;
};

}  // namespace b3native

#endif
