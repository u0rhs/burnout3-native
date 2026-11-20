// tuning_console.h -- the tuning console's game side (tune4, mod kit part 4: docs/modding/DESIGN.md §4,
// docs/modding/tuning.md; the model: include/b3tune/tune.h).
//
// A port-layer wrapper (HookTable, "port:tuning") on the game's three CGtCommsDatabase::AddEntry rows (float 0x0021BB60,
// int 0x0021BD80, vector 0x0021B9B0) and on CGtCommsDatabaseGame::InitialiseDefaultValues (0x0024FE90). Each AddEntry
// runs first; the wrapper then reads the registry entry it filled (the slot at database +4: the variable, its range,
// its three strings, its key, count and flags) and records it (b3tune::Registry, host side, in registration order).
// Read only, unless a preset or an edit asks for a write:
//   - presets (the active mods' `[tuning] preset`, in load order): written over the variable right after its
//     registration (the game has written its VDB.XML default then), and over every registered variable after
//     InitialiseDefaultValues (the defaults applied at load);
//   - edits (the console, a script, or a replay's recorded events): written at a VBlank boundary (vblank(), from
//     native_play's VBlank hook), to every live variable with the edit's key, and kept for the session: a later
//     registration of that key gets it after the preset.
// Installed only when the console is on (PC SETTINGS > MODS > DEVELOPER MODS, --tuning on), a mod has a preset,
// a script edits (--tune), or a recording being played has tuning edits. Otherwise nothing is wrapped: the game runs
// exactly as without this file.
//
// tunenav1: the player's race car (the console's YOUR CAR page): its boost, boost use rate and health, REPAIR, FILL
// BOOST, FREEZE DAMAGE and INFINITE BOOST (b3tune::car in include/b3tune/tune.h: the addresses). The page's edits are
// events with a car key, queued like the variables' edits, written on the game thread at the next VBlank (only while a
// race is on) and recorded the same way; FREEZE DAMAGE and INFINITE BOOST hold their value at each VBlank while on (a
// replay turns them on and off on the same VBlanks, so it holds them the same).
//
// tunenav2: defaults and resets. Each variable's default is the value the game gave it (after the active mods' presets),
// read at its registration and after InitialiseDefaultValues, before the session's edits are written over it. A value
// is changed while it has an edit and differs from that default (B3TuneHooks::changes: the console's CHANGED page). A
// reset (B3TuneHooks::reset: one element, a variable, or everything with the car page's toggles and boost use rate) is
// resolved at the VBlank into ordinary edits with the default's word, so pad recordings and replay files carry it and
// a replay writes the same words on the same VBlanks; the log marks such an edit "(the default)".
//
// Threads: the hooks and vblank() run on the game's thread; the renderer's B3TuneHooks functions on its own. One mutex
// (the registry, the values copied at each VBlank for the console, the console's queued edits).
#ifndef B3NATIVE_TUNING_CONSOLE_H
#define B3NATIVE_TUNING_CONSOLE_H

#include "hook_table.h"
#include "platform.h"

#include "b3tune/tune.h"
#include "b3tune/tune_abi.h"
#include "b3mod/mods.h"
#include "b3settings/settings.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace b3native {

class TuningConsole {
public:
    struct Options {
        bool console = false;                   // the console's UI is on (the renderer's DEVELOPER tab, F9)
        std::vector<b3tune::Preset> presets;    // the active mods' presets, in load order (the first wins)
        std::string script;                     // --tune / B3NATIVE_TUNE: "<vblank>:<name>=<value>;..."
        bool replaying = false;                 // a recording drives the edits (the console's and the script's are ignored)
        std::filesystem::path mods_dir;         // where "Save as preset" makes its mod
        bool log_registrations = false;         // B3NATIVE_TUNE_LOG=1: every registration in the log
    };
    static TuningConsole*& instance() { static TuningConsole* p = nullptr; return p; }
    ~TuningConsole() { if (instance() == this) instance() = nullptr; }

    bool install(HookTable& hooks, Options o);
    // native_play: where an applied edit is recorded (the pad recording's line, the replay file's SETS entry). Not
    // called for edits that came from a recording.
    std::function<void(const b3tune::Event&, const std::string& comment)> on_event;
    // a recorded edit (a pad recording's "tune" line, a replay file's "tune" entry): applied on its VBlank
    void replayed(const b3tune::Event& ev) { std::lock_guard<std::mutex> l(mu_); replay_q_.add(ev); }
    // game thread, each VBlank: this VBlank's edits, then (console on) the values for the console
    void vblank(std::uint64_t v);
    // the end of the run: one summary line
    void summary() const;
    const B3TuneHooks* renderer_hooks() const { return &abi_; }
    // sdk2b: the presets of mods left out after install (a code mod that did not load: the set is narrowed before the
    // game starts, so none of their values was written)
    void drop_presets(const std::set<std::string>& ids) {
        std::lock_guard<std::mutex> l(mu_);
        o_.presets.erase(std::remove_if(o_.presets.begin(), o_.presets.end(), [&](const b3tune::Preset& p) { return ids.count(p.mod) != 0; }),
                         o_.presets.end());
    }
    std::uint64_t calls() const { std::lock_guard<std::mutex> l(mu_); return reg_.calls(); }

private:
    static void add_float(const b3host::CallArgs& a, b3host::CallResult& r);
    static void add_int(const b3host::CallArgs& a, b3host::CallResult& r);
    static void add_vector(const b3host::CallArgs& a, b3host::CallResult& r);
    static void init_defaults(const b3host::CallArgs& a, b3host::CallResult& r);
    void after_add(std::uint32_t entry_point, const b3host::CallArgs& a);
    void apply_presets(std::size_t i, const char* when);       // mu_ held
    void apply_overrides(std::size_t i);                         // mu_ held
    void capture_default(std::size_t i);                         // mu_ held (tunenav2)
    // mu_ held: element `el` of entry i differs from its default because of an edit (`guest`: read the game's RAM, the
    // game thread; else the console's copy); *cur its value now
    bool changed(std::size_t i, std::uint32_t el, bool guest, std::uint32_t* cur = nullptr) const;
    std::optional<std::uint32_t> now(std::size_t i, std::uint32_t el, bool guest) const;   // nullopt: not live
    void resolve_reset(std::size_t index, int element, std::uint64_t v);   // mu_ held, game thread: the reset's edits, applied
    bool live(const b3tune::Entry& e) const;                     // game thread
    void write(const b3tune::Entry& e, std::uint32_t element, std::uint32_t word);
    void apply_event(const b3tune::Event& ev, bool record);      // mu_ held
    void resolve_script(const b3tune::ScriptEdit& s, std::uint64_t v, std::vector<b3tune::Event>& out);   // mu_ held
    static std::string read_string(std::uint32_t a);
    // B3TuneHooks
    static int c_count();
    static int c_get(int i, B3TuneVar* out);
    static void c_edit(int i, int element, std::uint32_t word);
    static int c_save(char* msg, int n);
    static int c_status(char* text, int n);
    static std::uint32_t c_generation();
    static int c_car(B3TuneCar* out);
    static void c_car_edit(int field, std::uint32_t word);
    static int c_changes(B3TuneChange* out, int n);
    std::vector<B3TuneChange> list_changes() const;   // mu_ held
    bool reset_queued(std::size_t i, std::uint32_t el) const;   // mu_ held
    static void c_reset(int i, int element);
    // tunenav1: the player's car
    static bool race_on();
    void apply_car(const b3tune::Event& ev, bool record);   // mu_ held
    void hold_car();                                        // mu_ held, each VBlank
    void snap_car();                                        // mu_ held, each VBlank (console on)

    Options o_;
    HookEntry next_float_ = nullptr, next_int_ = nullptr, next_vector_ = nullptr, next_init_ = nullptr;
    mutable std::mutex mu_;
    b3tune::Registry reg_;
    b3tune::Overrides overrides_;
    b3tune::EventQueue replay_q_;
    std::vector<b3tune::ScriptEdit> script_;
    std::size_t script_i_ = 0;
    // reset (tunenav2): back to the default (element kAllElements: each; index kAllValues: every changed value and the car's)
    static constexpr std::uint32_t kAllElements = 0xFFFFFFFFu;
    static constexpr std::size_t kAllValues = ~std::size_t(0);
    struct UiEdit { std::size_t index; std::uint32_t element, word; bool reset = false; };
    std::vector<UiEdit> ui_q_;
    struct CarEdit { std::uint32_t field, word; };
    std::vector<CarEdit> car_q_;
    b3tune::car::State car_state_;           // FREEZE DAMAGE / INFINITE BOOST
    B3TuneCar car_snap_{};
    std::uint32_t gen_ = 1;                  // B3TuneHooks::generation
    struct Snap { bool live = false; std::uint32_t edited = 0; std::uint32_t words[B3TUNE_MAX_WORDS] = {}; };
    std::vector<Snap> snap_;
    b3tune::Defaults def_;                   // tunenav2: each entry's default
    std::uint64_t by_type_[3] = {0, 0, 0};   // float, int, vector calls
    std::uint64_t preset_writes_ = 0, edit_writes_ = 0, events_ = 0, inits_ = 0, odd_ = 0;
    unsigned preset_logged_ = 0;
    std::vector<std::string> notes_seen_;
    B3TuneHooks abi_{};
};

// native_play's side, kept here so that native_play.cpp only calls it: the active mods' presets (in load order), and
// whether a recording being played has tuning edits
inline std::vector<b3tune::Preset> tuning_presets(const b3mod::Overlay& mods) {
    std::vector<b3tune::Preset> out;
    for (const b3mod::ActiveMod& m : mods.mods()) {
        if (m.tuning.empty()) continue;
        std::ifstream f(m.tuning, std::ios::binary);
        const std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        out.push_back(b3tune::parse_preset(text, m.id, m.tuning_rel));
    }
    return out;
}
// PC SETTINGS > MODS > DEVELOPER MODS as a window run starts: the renderer's settings file (--settings in its
// arguments, else <user data folder>/pc_settings.ini, as the renderer picks it; B3_NO_USER_SETTINGS: none). native_play
// reads it itself: the renderer loads it only once its window runs, after the table is sealed.
// The pc_settings.ini a run's renderer reads: --settings in its arguments, else the preferences folder's (none with
// B3_NO_USER_SETTINGS, as the offscreen checks run); empty when there is none.
inline std::string settings_path(const std::string& render_args, const std::filesystem::path& pref) {
    std::string path;
    if (const std::size_t at = render_args.find("--settings "); at != std::string::npos) {
        std::size_t b = at + 11;
        while (b < render_args.size() && render_args[b] == ' ') ++b;
        std::size_t e = b;
        while (e < render_args.size() && render_args[e] != ' ') ++e;
        path = render_args.substr(b, e - b);
    } else if (!pref.empty() && !std::getenv("B3_NO_USER_SETTINGS")) {
        path = (pref / "pc_settings.ini").string();
    }
    return path;
}
inline bool developer_mods_on(const std::string& render_args, const std::filesystem::path& pref) {
    const std::string path = settings_path(render_args, pref);
    b3settings::Settings s;
    return !path.empty() && b3settings::load_file(path, s) && s.developer_mods != 0;
}
inline std::string tuning_gameplay_ids(const b3mod::Overlay& mods) {   // "tuning a,b": the mods that change gameplay
    std::string ids;
    for (const b3mod::ActiveMod& m : mods.mods())
        if (m.changes_gameplay()) ids += (ids.empty() ? "" : ",") + m.id;
    return ids.empty() ? ids : "tuning " + ids;
}

inline std::string TuningConsole::read_string(std::uint32_t a) {
    std::string s;
    if (!a || !b3host::host_pointer(a, 1)) return s;
    for (std::uint32_t i = 0; i < 256; ++i) {
        if (!b3host::host_pointer(a + i, 1)) break;
        const char c = static_cast<char>(b3host::load<std::uint8_t>(a + i));
        if (!c) break;
        s.push_back(c);
    }
    return s;
}

inline bool TuningConsole::install(HookTable& hooks, Options o) {
    o_ = std::move(o);
    for (const b3tune::Preset& p : o_.presets) {
        std::printf("native_play: tuning: preset of %s (%s): %zu line%s%s; this mod changes gameplay\n", p.mod.c_str(), p.path.c_str(),
                    p.lines.size(), p.lines.size() == 1 ? "" : "s", p.errors.empty() ? "" : (", " + std::to_string(p.errors.size()) + " not usable").c_str());
        for (const std::string& e : p.errors) std::printf("native_play: tuning: %s %s: %s\n", p.mod.c_str(), p.path.c_str(), e.c_str());
    }
    std::vector<std::string> errs;
    script_ = b3tune::parse_script(o_.script, &errs);
    for (const std::string& e : errs) std::fprintf(stderr, "native_play: tuning: --tune: %s\n", e.c_str());
    if (!errs.empty()) return false;
    std::string error;
    const std::vector<Hook> set = {{b3tune::kAddFloat, HookLayer::kPort, &add_float, &next_float_},
                                   {b3tune::kAddInt, HookLayer::kPort, &add_int, &next_int_},
                                   {b3tune::kAddVector, HookLayer::kPort, &add_vector, &next_vector_},
                                   {b3tune::kInitialiseDefaults, HookLayer::kPort, &init_defaults, &next_init_}};
    if (!hooks.install("port:tuning", set, &error)) {
        std::fprintf(stderr, "native_play: tuning: %s: no tuning console, no presets\n", error.c_str());
        return false;
    }
    instance() = this;
    abi_.size = sizeof(B3TuneHooks);
    abi_.count = &c_count;
    abi_.get = &c_get;
    abi_.edit = &c_edit;
    abi_.save_preset = &c_save;
    abi_.status = &c_status;
    abi_.generation = &c_generation;
    abi_.car = &c_car;
    abi_.car_edit = &c_car_edit;
    abi_.changes = &c_changes;
    abi_.reset = &c_reset;
    std::printf("native_play: tuning: the registry is recorded (AddEntry 0x%08X / 0x%08X / 0x%08X wrapped)%s%s%s%s\n", b3tune::kAddFloat,
                b3tune::kAddInt, b3tune::kAddVector, o_.console ? "; the console is on (DEVELOPER, F9)" : "",
                o_.presets.empty() ? "" : ("; " + std::to_string(o_.presets.size()) + " preset(s)").c_str(),
                script_.empty() ? "" : ("; " + std::to_string(script_.size()) + " scripted edit(s)").c_str(),
                o_.replaying ? "; the edits come from the recording" : "");
    std::fflush(stdout);
    return true;
}

inline bool TuningConsole::live(const b3tune::Entry& e) const {
    if (e.dropped || !e.db) return false;
    const std::uint32_t base = b3host::load<std::uint32_t>(e.db + b3tune::kDbEntries);
    const std::uint32_t at = base + e.slot * b3tune::kEntrySize;
    if (!base || !b3host::host_pointer(at, b3tune::kEntrySize)) return false;
    return b3host::load<std::uint8_t>(at + 0x1E) != 0 && b3host::load<std::uint32_t>(at) == e.address &&
           b3host::load<std::int32_t>(at + 0x18) == e.key;
}

inline void TuningConsole::write(const b3tune::Entry& e, std::uint32_t element, std::uint32_t word) {
    if (element >= e.words()) return;
    const std::uint32_t a = e.address + 4u * element;
    if (!b3host::host_pointer(a, 4)) return;
    b3host::store<std::uint32_t>(a, word);
}

inline void TuningConsole::add_float(const b3host::CallArgs& a, b3host::CallResult& r) {
    TuningConsole* p = instance();
    p->next_float_(a, r);
    p->after_add(b3tune::kAddFloat, a);
}
inline void TuningConsole::add_int(const b3host::CallArgs& a, b3host::CallResult& r) {
    TuningConsole* p = instance();
    p->next_int_(a, r);
    p->after_add(b3tune::kAddInt, a);
}
inline void TuningConsole::add_vector(const b3host::CallArgs& a, b3host::CallResult& r) {
    TuningConsole* p = instance();
    p->next_vector_(a, r);
    p->after_add(b3tune::kAddVector, a);
}
inline void TuningConsole::init_defaults(const b3host::CallArgs& a, b3host::CallResult& r) {
    TuningConsole* p = instance();
    p->next_init_(a, r);
    std::lock_guard<std::mutex> l(p->mu_);
    ++p->inits_;
    std::size_t n = 0;
    for (std::size_t i = 0; i < p->reg_.size(); ++i)
        if (p->live(p->reg_.at(i))) {
            p->apply_presets(i, "after the defaults were loaded");
            p->capture_default(i);
            p->apply_overrides(i);
            ++n;
        }
    std::printf("native_play: VBlank %llu: tuning: the defaults were loaded (InitialiseDefaultValues): %zu variables registered then\n",
                static_cast<unsigned long long>(platform::vblank_count()), n);
}

inline void TuningConsole::after_add(std::uint32_t entry_point, const b3host::CallArgs& a) {
    const std::uint32_t db = a.gpr[0].w[0], var = a.gpr[1].w[0];
    if (!b3host::host_pointer(db, 0x14)) return;
    const std::uint32_t slot = b3host::load<std::uint32_t>(db + b3tune::kDbSlot), base = b3host::load<std::uint32_t>(db + b3tune::kDbEntries),
                        cap = b3host::load<std::uint32_t>(db + b3tune::kDbCapacity);
    const std::uint32_t at = base + slot * b3tune::kEntrySize;
    std::lock_guard<std::mutex> l(mu_);
    if (slot >= cap || !b3host::host_pointer(at, b3tune::kEntrySize) || b3host::load<std::uint32_t>(at) != var) {
        if (++odd_ <= 5) std::printf("native_play: tuning: an AddEntry for %08X left no entry where expected (slot %u of %u): not recorded\n", var, slot, cap);
        return;
    }
    b3tune::Registration g;
    g.entry_point = entry_point;
    g.db = db;
    g.slot = slot;
    g.address = var;
    g.type = entry_point == b3tune::kAddFloat ? b3tune::Type::kFloat : entry_point == b3tune::kAddInt ? b3tune::Type::kInt : b3tune::Type::kVector;
    const bool vec = g.type == b3tune::Type::kVector;
    g.max_word = vec ? 0 : b3host::load<std::uint32_t>(at + 0x4);
    g.min_word = vec ? 0 : b3host::load<std::uint32_t>(at + 0x8);
    g.name = read_string(b3host::load<std::uint32_t>(at + 0xC));
    g.group = read_string(b3host::load<std::uint32_t>(at + 0x10));
    g.file = read_string(b3host::load<std::uint32_t>(at + 0x14));
    g.key = b3host::load<std::int32_t>(at + 0x18);
    g.count = vec ? 0 : b3host::load<std::uint8_t>(at + 0x1C);
    if (g.count > B3TUNE_MAX_WORDS) g.count = B3TUNE_MAX_WORDS;   // (retail's largest is 8)
    g.flags = b3host::load<std::uint8_t>(at + 0x1D);
    g.dropped = b3host::load<std::uint8_t>(at + 0x1E) == 0;   // its config file is disabled: unregistered at once
    ++by_type_[g.type == b3tune::Type::kFloat ? 0 : g.type == b3tune::Type::kInt ? 1 : 2];
    const std::size_t i = reg_.record(g);
    if (o_.log_registrations) {
        const b3tune::Entry& e = reg_.at(i);
        std::string value;   // as the game left it (its VDB.XML default, or its own)
        for (std::uint32_t w = 0; w < e.words(); ++w)
            value += (w ? ", " : "") + b3tune::format_word(e, b3host::load<std::uint32_t>(e.address + 4u * w));
        if (e.words() > 1) value = (e.type == b3tune::Type::kVector ? "(" : "[") + value + (e.type == b3tune::Type::kVector ? ")" : "]");
        std::printf("native_play: VBlank %llu: tuning: #%llu %s %s (%s)%s = %s key %s at %08X%s%s\n",
                    static_cast<unsigned long long>(platform::vblank_count()), static_cast<unsigned long long>(reg_.calls()), e.type_name(),
                    e.label().c_str(), e.file.c_str(), e.count >= 2 ? (" x" + std::to_string(e.count)).c_str() : "", value.c_str(),
                    b3tune::hex8(std::uint32_t(e.key)).c_str(), e.address,
                    e.ranged() ? (" range " + b3tune::format_word(e, e.min_word) + " .. " + b3tune::format_word(e, e.max_word)).c_str() : "",
                    e.dropped ? " (file disabled)" : "");
    }
    apply_presets(i, "registered");
    capture_default(i);
    apply_overrides(i);
}

inline void TuningConsole::capture_default(std::size_t i) {
    const b3tune::Entry& e = reg_.at(i);
    std::uint32_t w[B3TUNE_MAX_WORDS] = {};
    for (std::uint32_t k = 0; k < e.words() && k < B3TUNE_MAX_WORDS; ++k)
        w[k] = b3host::host_pointer(e.address + 4u * k, 4) ? b3host::load<std::uint32_t>(e.address + 4u * k) : 0u;
    def_.capture(i, e, w);
}

inline std::optional<std::uint32_t> TuningConsole::now(std::size_t i, std::uint32_t el, bool guest) const {
    const b3tune::Entry& e = reg_.at(i);
    if (el >= B3TUNE_MAX_WORDS) return std::nullopt;
    if (guest) {
        if (live(e) && b3host::host_pointer(e.address + 4u * el, 4)) return b3host::load<std::uint32_t>(e.address + 4u * el);
        return std::nullopt;
    }
    if (i < snap_.size() && snap_[i].live) return snap_[i].words[el];
    return std::nullopt;
}
inline bool TuningConsole::changed(std::size_t i, std::uint32_t el, bool guest, std::uint32_t* cur) const {
    if (i >= reg_.size()) return false;
    return def_.changed(reg_, overrides_, i, el, now(i, el, guest), cur);
}

inline void TuningConsole::resolve_reset(std::size_t index, int element, std::uint64_t v) {
    auto guest = [&](std::size_t i, std::uint32_t el) { return now(i, el, true); };
    std::vector<b3tune::Event> evs = def_.resets(reg_, overrides_, v, index == kAllValues ? b3tune::Defaults::kAll : index, element, guest);
    if (index == kAllValues) {   // and the car page's (FREEZE DAMAGE, INFINITE BOOST off; the game's boost use rate)
        namespace c = b3tune::car;
        if (car_state_.freeze) evs.push_back({v, c::key_of(c::kFreezeDamage), 0, 0});
        if (car_state_.infinite) evs.push_back({v, c::key_of(c::kInfiniteBoost), 0, 0});
        if (car_state_.rate_set && race_on()) evs.push_back({v, c::key_of(c::kBoostRate), 0, b3tune::float_word(car_state_.rate_game)});
        std::printf("native_play: VBlank %llu: tuning: reset all: %zu value%s back to the default\n", static_cast<unsigned long long>(v),
                    evs.size(), evs.size() == 1 ? "" : "s");
    }
    for (const b3tune::Event& ev : evs) apply_event(ev, true);
}

inline void TuningConsole::apply_presets(std::size_t i, const char* when) {
    if (o_.presets.empty()) return;
    const b3tune::Entry& e = reg_.at(i);
    std::vector<std::string> notes;
    const std::vector<b3tune::Applied> got = b3tune::preset_values(o_.presets, e, &notes);
    for (const std::string& n : notes)
        if (std::find(notes_seen_.begin(), notes_seen_.end(), n) == notes_seen_.end()) {
            notes_seen_.push_back(n);
            std::printf("native_play: tuning: %s\n", n.c_str());
        }
    for (const b3tune::Applied& ap : got) {
        const std::uint32_t was = b3host::load<std::uint32_t>(e.address + 4u * ap.element);
        write(e, ap.element, ap.word);
        ++preset_writes_;
        if (preset_logged_ < 200) {
            ++preset_logged_;
            std::printf("native_play: VBlank %llu: tuning: %s %s: %s (%s) = %s (was %s; %s line %d)\n",
                        static_cast<unsigned long long>(platform::vblank_count()), ap.mod.c_str(), when, e.element_label(ap.element).c_str(),
                        e.file.c_str(), b3tune::format_word(e, ap.word).c_str(), b3tune::format_word(e, was).c_str(), ap.mod.c_str(), ap.line);
            if (preset_logged_ == 200) std::printf("native_play: tuning: (further preset writes are counted, not logged)\n");
        }
    }
}

inline void TuningConsole::apply_overrides(std::size_t i) {
    if (overrides_.empty()) return;
    const b3tune::Entry& e = reg_.at(i);
    for (const auto& [el, w] : overrides_.of(e.key)) {
        write(e, el, w);
        ++edit_writes_;
    }
}

inline void TuningConsole::apply_event(const b3tune::Event& ev, bool record) {
    if (b3tune::car::is_key(ev.key)) { apply_car(ev, record); return; }   // tunenav1: the player's car
    overrides_.set(ev.key, ev.element, ev.word);
    std::size_t n = 0;
    const b3tune::Entry* first = nullptr;
    for (std::size_t i : reg_.with_key(ev.key)) {
        const b3tune::Entry& e = reg_.at(i);
        if (!first) first = &e;
        if (!live(e) || ev.element >= e.words()) continue;
        write(e, ev.element, ev.word);
        ++edit_writes_;
        ++n;
        if (i < snap_.size()) snap_[i].edited |= 1u << (ev.element & 31u);
    }
    ++events_;
    std::string what = first ? first->element_label(ev.element) + " = " + b3tune::format_word(*first, ev.word) : b3tune::event_value(ev);
    if (first && def_.known(first->index) && def_.word(first->index, ev.element) == ev.word)
        what += " (the default)";   // tunenav2: a reset (or an edit back to it)
    std::printf("native_play: VBlank %llu: tuning: %s (%s) on %zu variable%s%s\n", static_cast<unsigned long long>(ev.vblank), what.c_str(),
                first ? first->file.c_str() : "not registered yet", n, n == 1 ? "" : "s", record ? "" : " (from the recording)");
    std::fflush(stdout);
    if (record && on_event) on_event(ev, what);
}

inline void TuningConsole::resolve_script(const b3tune::ScriptEdit& s, std::uint64_t v, std::vector<b3tune::Event>& out) {
    std::vector<std::int32_t> keys;
    for (const b3tune::Entry& e : reg_.entries()) {
        if (!s.target.matches(e) || !live(e) || std::find(keys.begin(), keys.end(), e.key) != keys.end()) continue;
        keys.push_back(e.key);
        std::vector<std::pair<std::uint32_t, std::uint32_t>> ws;
        std::string why;
        if (!b3tune::parse_value(s.value, e, s.target.element, ws, &why)) {
            std::printf("native_play: VBlank %llu: tuning: --tune %s: %s\n", static_cast<unsigned long long>(v), e.label().c_str(), why.c_str());
            continue;
        }
        for (const auto& [el, w] : ws) {
            if (el >= e.elements()) continue;   // a vector's w stays the game's
            out.push_back({v, e.key, el, b3tune::clamp_word(e, w)});
        }
    }
    if (keys.empty())
        std::printf("native_play: VBlank %llu: tuning: --tune: no registered variable is %s%s\n", static_cast<unsigned long long>(v),
                    s.target.by_key ? b3tune::hex8(std::uint32_t(s.target.key)).c_str() : (s.target.group + " | " + s.target.name).c_str(),
                    " now: nothing written");
}

inline void TuningConsole::vblank(std::uint64_t v) {
    std::lock_guard<std::mutex> l(mu_);
    if (o_.replaying) {
        for (b3tune::Event ev : replay_q_.take(v)) {
            ev.vblank = v;
            apply_event(ev, false);
        }
    } else {
        std::vector<b3tune::Event> evs;
        for (; script_i_ < script_.size() && script_[script_i_].vblank <= v; ++script_i_) resolve_script(script_[script_i_], v, evs);
        for (const b3tune::Event& ev : evs) apply_event(ev, true);
        // the console's, in the order asked (tunenav2: a reset sees the edits before it)
        for (const UiEdit& u : ui_q_) {
            if (u.reset) { resolve_reset(u.index, u.element == kAllElements ? -1 : int(u.element), v); continue; }
            if (u.index >= reg_.size()) continue;
            const b3tune::Entry& e = reg_.at(u.index);
            if (u.element >= e.elements()) continue;
            apply_event({v, e.key, u.element, b3tune::clamp_word(e, u.word)}, true);
        }
        ui_q_.clear();
        for (const CarEdit& c : car_q_) apply_event({v, b3tune::car::key_of(b3tune::car::Field(c.field)), 0, c.word}, true);
        car_q_.clear();
    }
    hold_car();   // tunenav1: FREEZE DAMAGE / INFINITE BOOST
    if (!o_.console) return;
    // the values for the console (read only)
    if (snap_.size() < reg_.size()) { snap_.resize(reg_.size()); ++gen_; }
    for (std::size_t i = 0; i < reg_.size(); ++i) {
        const b3tune::Entry& e = reg_.at(i);
        Snap& s = snap_[i];
        const bool was = s.live;
        s.live = live(e);
        if (s.live != was) ++gen_;
        if (!s.live) continue;
        for (std::uint32_t w = 0; w < e.words() && w < B3TUNE_MAX_WORDS; ++w)
            s.words[w] = b3host::host_pointer(e.address + 4u * w, 4) ? b3host::load<std::uint32_t>(e.address + 4u * w) : 0u;
    }
    snap_car();
}

// ---- tunenav1: the player's race car (the writes: b3tune::car::apply / hold, include/b3tune/tune.h) ----
// guest RAM for b3tune::car's templates (an address outside guest RAM reads 0 and is not written)
struct GuestMem {
    std::uint32_t load32(std::uint32_t a) const { return b3host::host_pointer(a, 4) ? b3host::load<std::uint32_t>(a) : 0u; }
    std::uint8_t load8(std::uint32_t a) const { return b3host::host_pointer(a, 1) ? b3host::load<std::uint8_t>(a) : 0u; }
    void store32(std::uint32_t a, std::uint32_t w) const { if (b3host::host_pointer(a, 4)) b3host::store<std::uint32_t>(a, w); }
    void store8(std::uint32_t a, std::uint8_t w) const { if (b3host::host_pointer(a, 1)) b3host::store<std::uint8_t>(a, w); }
};
inline bool TuningConsole::race_on() {
    GuestMem m;
    return b3tune::car::race_on(m);
}
inline void TuningConsole::apply_car(const b3tune::Event& ev, bool record) {
    GuestMem m;
    bool wrote = false;
    const std::string what = "car: " + b3tune::car::apply(car_state_, m, ev.key, ev.word, wrote);
    ++events_;
    std::printf("native_play: VBlank %llu: tuning: %s on the player's car%s%s\n", static_cast<unsigned long long>(ev.vblank), what.c_str(),
                wrote ? "" : " (no race now: nothing written)", record ? "" : " (from the recording)");
    std::fflush(stdout);
    if (record && on_event) on_event(ev, what);
}
inline void TuningConsole::hold_car() {
    GuestMem m;
    b3tune::car::hold(car_state_, m);
}
inline void TuningConsole::snap_car() {
    namespace c = b3tune::car;
    B3TuneCar& s = car_snap_;
    s = B3TuneCar{};
    s.size = sizeof(B3TuneCar);
    s.freeze = car_state_.freeze ? 1u : 0u;
    s.infinite = car_state_.infinite ? 1u : 0u;
    s.changed = (car_state_.freeze ? 1u << c::kFreezeDamage : 0u) | (car_state_.infinite ? 1u << c::kInfiniteBoost : 0u);
    if (!race_on()) return;
    if (car_state_.rate_set) {
        s.changed |= 1u << c::kBoostRate;
        s.boost_rate_default = car_state_.rate_game;
    }
    const std::uint32_t car = c::kPlayerCar;
    auto f32 = [](std::uint32_t a) { return b3host::host_pointer(a, 4) ? b3tune::as_float(b3host::load<std::uint32_t>(a)) : 0.0f; };
    s.active = 1;
    s.car = car;
    s.speed = f32(c::kSpeedW);
    s.speed_factor = f32(b3host::host_pointer(c::kSpeedUnits, 4) && b3host::load<std::uint32_t>(c::kSpeedUnits) > 1 ? c::kSpeedFactorHigh
                                                                                                                      : c::kSpeedFactorLow);
    s.boost = f32(car + c::kBoostNow);
    s.boost_max = f32(car + c::kBoostMax);
    s.boost_rate = f32(car + c::kBoostRateOff);
    s.boosting = b3host::load<std::uint8_t>(car + c::kBoosting) ? 1u : 0u;
    s.infinite_flag = b3host::load<std::uint8_t>(car + c::kInfiniteFlag) ? 1u : 0u;
    s.health = f32(car + c::kHealthOff);
}

inline void TuningConsole::summary() const {
    std::lock_guard<std::mutex> l(mu_);
    std::vector<std::int32_t> keys;
    std::vector<std::string> names;
    for (const b3tune::Entry& e : reg_.entries()) {
        keys.push_back(e.key);
        names.push_back(e.label());
    }
    std::sort(keys.begin(), keys.end());
    std::sort(names.begin(), names.end());
    const std::size_t nk = std::size_t(std::unique(keys.begin(), keys.end()) - keys.begin());
    const std::size_t nn = std::size_t(std::unique(names.begin(), names.end()) - names.begin());
    std::printf("native_play: tuning: %llu AddEntry calls (float %llu, int %llu, vector %llu; %llu with their config file disabled), "
                "%zu variables, %zu keys, %zu names; defaults loaded %llu time(s); %llu preset writes; %llu edits (%llu writes)\n",
                static_cast<unsigned long long>(reg_.calls()), static_cast<unsigned long long>(by_type_[0]),
                static_cast<unsigned long long>(by_type_[1]), static_cast<unsigned long long>(by_type_[2]),
                static_cast<unsigned long long>(reg_.dropped_calls()), reg_.size(), nk, nn, static_cast<unsigned long long>(inits_),
                static_cast<unsigned long long>(preset_writes_), static_cast<unsigned long long>(events_),
                static_cast<unsigned long long>(edit_writes_));
}

// ---- B3TuneHooks (the renderer's thread) ----
inline int TuningConsole::c_count() {
    TuningConsole* p = instance();
    if (!p) return 0;
    std::lock_guard<std::mutex> l(p->mu_);
    return int(std::min<std::size_t>(p->snap_.size(), p->reg_.size()));
}
inline int TuningConsole::c_get(int i, B3TuneVar* out) {
    TuningConsole* p = instance();
    if (!p || !out || i < 0) return 0;
    std::lock_guard<std::mutex> l(p->mu_);
    if (std::size_t(i) >= p->reg_.size() || std::size_t(i) >= p->snap_.size()) return 0;
    const b3tune::Entry& e = p->reg_.at(std::size_t(i));
    const Snap& s = p->snap_[std::size_t(i)];
    *out = B3TuneVar{};
    out->index = std::uint32_t(e.index);
    out->key = e.key;
    out->address = e.address;
    out->type = std::uint32_t(e.type);
    out->count = e.count;
    out->elements = e.elements();
    out->flags = e.flags;
    out->min_word = e.min_word;
    out->max_word = e.max_word;
    out->live = s.live ? 1u : 0u;
    out->registrations = e.registrations;
    out->edited = s.edited;
    for (std::uint32_t w = 0; w < B3TUNE_MAX_WORDS; ++w) out->words[w] = s.words[w];
    out->group = e.group.c_str();
    out->name = e.name.c_str();
    out->file = e.file.c_str();
    return 1;
}
inline void TuningConsole::c_edit(int i, int element, std::uint32_t word) {
    TuningConsole* p = instance();
    if (!p || i < 0 || element < 0 || p->o_.replaying) return;
    std::lock_guard<std::mutex> l(p->mu_);
    p->ui_q_.push_back({std::size_t(i), std::uint32_t(element), word});
}
inline std::uint32_t TuningConsole::c_generation() {
    TuningConsole* p = instance();
    if (!p) return 0;
    std::lock_guard<std::mutex> l(p->mu_);
    return p->gen_;
}
inline int TuningConsole::c_car(B3TuneCar* out) {
    TuningConsole* p = instance();
    if (!p || !out || out->size < sizeof(B3TuneCar)) return 0;
    std::lock_guard<std::mutex> l(p->mu_);
    *out = p->car_snap_;
    out->size = sizeof(B3TuneCar);
    // tunenav2: what the queued car edits and RESET ALL will make of the changed bits (the console's counts at once)
    namespace c = b3tune::car;
    for (const UiEdit& u : p->ui_q_)
        if (u.reset && u.index == kAllValues) out->changed = 0;
    for (const CarEdit& e : p->car_q_) {
        const std::uint32_t bit = 1u << e.field;
        if (e.field == c::kFreezeDamage || e.field == c::kInfiniteBoost) out->changed = e.word ? out->changed | bit : out->changed & ~bit;
        else if (e.field == c::kBoostRate && e.word == b3tune::float_word(p->car_state_.rate_game)) out->changed &= ~bit;
    }
    return 1;
}
inline void TuningConsole::c_car_edit(int field, std::uint32_t word) {
    TuningConsole* p = instance();
    if (!p || p->o_.replaying || field < 0 || !b3tune::car::is_field(std::uint32_t(field))) return;
    std::lock_guard<std::mutex> l(p->mu_);
    p->car_q_.push_back({std::uint32_t(field), word});
}
inline int TuningConsole::c_changes(B3TuneChange* out, int n) {
    TuningConsole* p = instance();
    if (!p) return 0;
    std::lock_guard<std::mutex> l(p->mu_);
    const std::vector<B3TuneChange> all = p->list_changes();
    for (std::size_t k = 0; k < all.size() && out && int(k) < n; ++k) out[k] = all[k];
    return int(all.size());
}
// mu_ held: per edited key and element, its live entries that differ from their default; none live: the newest entry
// (an earlier race's, the same variable registered again elsewhere, is not listed twice)
inline std::vector<B3TuneChange> TuningConsole::list_changes() const {
    std::vector<B3TuneChange> all;
    for (const auto& [ke, word] : overrides_.all()) {
        (void)word;
        const std::vector<std::size_t>& idx = reg_.with_key(ke.first);
        bool any_live = false;
        for (std::size_t i : idx) any_live = any_live || (i < snap_.size() && snap_[i].live);
        for (auto it = idx.rbegin(); it != idx.rend(); ++it) {
            const std::size_t i = *it;
            const bool lv = i < snap_.size() && snap_[i].live;
            if (any_live != lv) continue;
            std::uint32_t cur = 0;
            if (!changed(i, ke.second, false, &cur) || reset_queued(i, ke.second)) continue;
            B3TuneChange c{};
            c.index = std::uint32_t(i);
            c.element = ke.second;
            c.word = cur;
            c.def = def_.word(i, ke.second);
            c.live = lv ? 1u : 0u;
            all.push_back(c);
            if (!lv) break;
        }
    }
    std::sort(all.begin(), all.end(), [](const B3TuneChange& a, const B3TuneChange& b) {
        return a.index != b.index ? a.index < b.index : a.element < b.element;
    });
    return all;
}
// mu_ held: a reset the console asked for, not written yet (the next VBlank), puts element el of entry i back (and no
// edit of it after the reset): the console's list leaves it out already, so its counts follow its keys at once
inline bool TuningConsole::reset_queued(std::size_t i, std::uint32_t el) const {
    bool r = false;
    for (const UiEdit& u : ui_q_) {
        if (u.reset && (u.index == kAllValues || (u.index == i && (u.element == kAllElements || u.element == el)))) r = true;
        else if (!u.reset && u.index == i && u.element == el) r = false;
    }
    return r;
}
inline void TuningConsole::c_reset(int i, int element) {
    TuningConsole* p = instance();
    if (!p || p->o_.replaying || i < -1) return;
    std::lock_guard<std::mutex> l(p->mu_);
    p->ui_q_.push_back({i < 0 ? kAllValues : std::size_t(i), element < 0 ? kAllElements : std::uint32_t(element), 0, true});
}
inline int TuningConsole::c_save(char* msg, int n) {
    TuningConsole* p = instance();
    auto say = [&](const std::string& t) { if (msg && n > 0) std::snprintf(msg, std::size_t(n), "%s", t.c_str()); };
    if (!p) { say("No tuning console in this run."); return 0; }
    std::string text;
    {
        std::lock_guard<std::mutex> l(p->mu_);
        const b3tune::Overrides kept = p->def_.kept(p->reg_, p->overrides_);   // tunenav2: not the edits back at their default
        if (kept.empty()) { say(p->overrides_.empty() ? "No edits yet: change a value first." : "Nothing changed now: every value is at its default."); return 0; }
        text = b3tune::overrides_to_preset(kept, p->reg_, "A tuning preset");
    }
    if (p->o_.mods_dir.empty()) { say("No mods folder in this run."); return 0; }
    const std::time_t now = std::time(nullptr);
    std::tm lt{};
#ifdef _WIN32
    localtime_s(&lt, &now);
#else
    localtime_r(&now, &lt);
#endif
    char id[64];
    std::strftime(id, sizeof id, "tuning-%Y%m%d-%H%M%S", &lt);
    namespace fs = std::filesystem;
    const fs::path dir = p->o_.mods_dir / id;
    std::error_code ec;
    fs::create_directories(dir / "tuning", ec);
    if (ec) { say("Cannot make " + dir.string() + ": " + ec.message()); return 0; }
    std::ofstream ini(dir / "mod.ini", std::ios::binary), pre(dir / "tuning" / "preset.ini", std::ios::binary);
    ini << "[mod]\nid = " << id << "\nname = Tuning preset " << (id + 7) << "\nversion = 1\napi = 1\n"
        << "description = Saved from the tuning console. Changes gameplay.\n\n[tuning]\npreset = tuning/preset.ini\n";
    pre << text;
    if (!ini || !pre) { say("Cannot write the preset in " + dir.string()); return 0; }
    std::printf("native_play: tuning: the edits saved as the mod %s (%s)\n", id, dir.string().c_str());
    std::fflush(stdout);
    say(std::string("Saved as the mod ") + id + " (off until you turn it on in the launcher's Mods page).");
    return 1;
}
inline int TuningConsole::c_status(char* text, int n) {
    TuningConsole* p = instance();
    if (!p || !text || n <= 0) return 0;
    std::lock_guard<std::mutex> l(p->mu_);
    std::size_t live = 0;
    for (const Snap& s : p->snap_) live += s.live ? 1 : 0;
    const std::size_t changed = p->list_changes().size();   // tunenav2: the values that differ from their defaults now
    std::string t = std::to_string(live) + " variables registered now, " + std::to_string(p->reg_.size()) + " seen; " +
                    std::to_string(changed) + " changed from their defaults.";
    if (!p->o_.presets.empty()) {
        t += " Presets:";
        for (const b3tune::Preset& pr : p->o_.presets) t += " " + pr.mod;
        t += ".";
    }
    if (p->o_.replaying) t += " A replay drives the values: edits are off.";
    std::snprintf(text, std::size_t(n), "%s", t.c_str());
    return 1;
}

}  // namespace b3native

#endif
