// codemod.h -- the parts of the code-mod SDK (mod kit Phase 2b, docs/modding/DESIGN.md §2) that are not the game:
// settings rows and their values, the live settings with their recording and replay, the trust store, the last start's
// report for the launcher, and the overlay record. Standard library only: native_play (src/native/code_mods.h), the
// renderer (the PC SETTINGS page's MODS tab, the overlay), the launcher (the Mods page) and the tests share it.
//
// Files in the user data folder (window runs only; offscreen runs never read or write them):
//   mod-settings/<id>.ini   a code mod's settings values: "[settings]" then "<key> = <value>" (Off / On, a choice's
//                           name, a slider's level number); written by native_play when a value changes, and by the
//                           launcher's Mods page
//   mods-trusted.ini        the code-mod libraries the user allowed to run: "[trusted]" then "<sha-256> = <id> <version>"
//                           (the launcher asks once per library; native_play refuses a library not listed, in a window)
//   mods-state.ini          what the last window start did with each code mod ([mod <id>]: status, reason, rows),
//                           written by native_play, read by the launcher's Mods page
#ifndef B3MOD_CODEMOD_H
#define B3MOD_CODEMOD_H

#include "b3mod/mods.h"

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace b3mod {

// ---- settings rows (B3ModApi.add_setting) ----
enum class SettingKind : std::uint8_t { kToggle = 0, kChoice = 1, kSlider = 2 };
struct SettingRow {
    std::string mod;           // the mod's id
    std::string key;           // the row's key in the mod (valid_setting_key)
    std::string label, description;
    SettingKind kind = SettingKind::kToggle;
    std::vector<std::string> values;   // a choice's names; a toggle: Off, On
    unsigned count = 2;                // values (toggle 2, choice 2..16) or levels (slider 2..101)
    unsigned def = 0;                  // the default index
    int value = 0;                     // the current index
    std::string qualified() const { return mod + "." + key; }
};
bool valid_setting_key(std::string_view key);   // a-z 0-9 _ - . ; 1..32
// A row as add_setting gives it, checked: "" when it is fine, else why not (one line)
std::string check_row(const SettingRow& r);
// The value's text as files and logs show it (Off / On, a choice's name, a slider's level number), and back
// (case-insensitive name, or the index as a number); -1: not a value of the row
std::string value_text(const SettingRow& r, int index);
int value_from_text(const SettingRow& r, std::string_view text);

// The rows as text (one line each; the renderer's page and mods-state.ini): "<mod>|<key>|<kind>|<count>|<def>|<value>|
// <label>|<description>|<v1>;<v2>;..." with '\' '|' ';' and newlines escaped (\\ \p \s \n)
std::string row_to_line(const SettingRow& r);
bool row_from_line(std::string_view line, SettingRow& r);
std::string rows_to_text(const std::vector<SettingRow>& rows);
std::vector<SettingRow> rows_from_text(std::string_view text);

// ---- a mod's values file ----
fs::path settings_folder(const fs::path& pref);                        // <pref>/mod-settings
fs::path settings_file(const fs::path& pref, const std::string& id);   // <pref>/mod-settings/<id>.ini
std::map<std::string, std::string> load_values(const fs::path& file);  // key -> text (no file: empty)
// Writes every row of `id` in `rows` (others are skipped) with its current value
bool save_values(const fs::path& file, const std::string& id, const std::vector<SettingRow>& rows, std::string* why = nullptr);

// ---- the live settings: values change only at VBlank boundaries; recorded; replayed ----
// One change: "set <vblank> <mod>.<key> <value text>" in a pad recording, "mod.<mod>.<key>" = <value text> in a replay
struct SettingSet {
    std::uint64_t vblank = 0;
    std::string mod, key, value;   // value: text (value_text / value_from_text)
    bool operator==(const SettingSet&) const = default;
};
// "[<vblank>:]<mod>.<key>=<value>[,...]" (native_play --mod-set; no vblank: from the start) -> sets; false: a bad item
bool parse_set_spec(std::string_view spec, std::vector<SettingSet>& out, std::string* why = nullptr);
// A pad recording's line "set <vblank> <mod>.<key> <value>" (value to the line's end) and back
std::string set_line(const SettingSet& s);
bool set_from_line(std::string_view line, SettingSet& s);

class LiveSettings {
public:
    // A row (its value: `value`, as loaded); its index, or -1 when one with the same mod and key is there
    int add(const SettingRow& row);
    void remove_mod(const std::string& mod);          // a mod that did not load: its rows go
    int find(std::string_view mod, std::string_view key) const;
    const std::vector<SettingRow>& rows() const { return rows_; }
    int value(int index) const { return index >= 0 && std::size_t(index) < rows_.size() ? rows_[std::size_t(index)].value : -1; }

    // Playback: only `events` change values (each at its VBlank); requests are ignored (the recording decides)
    void play(std::vector<SettingSet> events);
    bool playing() const { return playing_; }
    // A change asked for (the page, the launcher, a script): applied at the next boundary(); unknown rows and values
    // are dropped (false)
    bool request(std::string_view mod, std::string_view key, std::string_view value_text);
    // Scripted changes (--mod-set) at their VBlanks (playback ignores them too)
    void schedule(std::vector<SettingSet> sets);

    // The VBlank boundary `v`: the changes that take effect now, in order (requests in the order made, then the
    // scheduled / recorded ones for v), each applied. A change to the value a row already has is not returned.
    std::vector<SettingSet> boundary(std::uint64_t v);
    // Every row's value now, as changes at VBlank `v` (the recording's start: "set 0 ...")
    std::vector<SettingSet> snapshot(std::uint64_t v = 0) const;

private:
    bool apply(const SettingSet& s, std::vector<SettingSet>& out);
    std::vector<SettingRow> rows_;
    std::vector<SettingSet> requests_, timed_;   // timed_: by VBlank (stable)
    std::size_t timed_at_ = 0;
    bool playing_ = false;
};

// ---- trust (DESIGN.md §2.5) ----
fs::path trust_file(const fs::path& pref);   // <pref>/mods-trusted.ini
// SHA-256 of a file's bytes, 64 hex digits ("" : unreadable)
std::string library_digest(const fs::path& library);
bool trusted(const fs::path& pref, const std::string& digest);
bool add_trust(const fs::path& pref, const std::string& digest, const std::string& id, const std::string& version, std::string* why = nullptr);

// ---- the last window start's report (native_play writes it, the launcher's Mods page reads it) ----
struct ModState {
    std::string id;
    bool loaded = false;
    std::string reason;                // why it was not loaded ("" when loaded)
    std::string digest;                // the library's SHA-256
    std::vector<SettingRow> rows;      // the rows it added (values as they were at the start)
};
fs::path state_file(const fs::path& pref);   // <pref>/mods-state.ini
std::string state_to_ini(const std::vector<ModState>& s);
std::vector<ModState> state_from_ini(std::string_view text);
bool save_state(const fs::path& pref, const std::vector<ModState>& s, std::string* why = nullptr);
std::vector<ModState> load_state(const fs::path& pref);

}  // namespace b3mod

#include "b3mod/overlay_record.h"   // the overlay record (header only: the renderer's decoder uses it alone)

#endif
