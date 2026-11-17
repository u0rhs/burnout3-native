// pad_record.h -- native_play's pad recording and replay (mac4, 2026-11-08): the 32-byte DS2 report the window gave on
// each virtual VBlank, written when it changes, so a run on another machine (the Mac) can be replayed here exactly.
// The game runs on virtual time (VBlanks, IOP samples, idle skip to the next event), so the same reports on the same
// VBlanks give the same run [L: x86-64 and aarch64 agree call for call, docs/mac.md §6].
//
// File (text): a header line "B3PADREC 1", then "<vblank> <64 hex digits>" per change (the report as the pad model
// gets it, before the movie skip's mask), and "end <vblank>" when the run ends. mac5: "rtc <unix>,<offset>" after the
// header when the run read the player's clock ($B3NATIVE_RTC, save_card.h), so a replay reads the same. modkit1: "mods
// <set hash> <ids>" when mods were active (include/b3mod/mods.h; none: no line), so a replay can say it differs. rumble1:
// "rumble <vblank> 0|1" when the pads' actuator requests start / stop being acknowledged (PC SETTINGS > RUMBLE; the
// game sees the answers, platform_sif.h set_actuator_ack), so a replay answers the same; none while it stays off (older
// readers skip the line). sdk2b: "set <vblank> <mod>.<key> <value>" for the code mods' settings (every row's value as
// "set 0 ..." after the header, then each change at the VBlank it took effect; include/b3mod/codemod.h set_line), so
// a replay changes them on the same VBlanks.
// tune4: "tune <vblank> 0x<key>[<element>] 0x<word>  # <name> = <value>" per tuning console
// edit (include/b3tune/tune.h; the write a replay makes again on that VBlank), and "gameplay <ids>" after the mods line
// when a mod changes gameplay (a tuning preset). No game data.
#ifndef B3NATIVE_PAD_RECORD_H
#define B3NATIVE_PAD_RECORD_H

#include "b3mod/codemod.h"   // sdk2b: the "set" lines

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace b3native {

using PadReport = std::array<unsigned char, 32>;

class PadRecorder {
public:
    ~PadRecorder() { close(0); }
    bool open(const std::string& path) {
        f_ = std::fopen(path.c_str(), "w");
        if (!f_) return false;
        std::fprintf(f_, "B3PADREC 1\n");
        return true;
    }
    bool is_open() const { return f_ != nullptr; }
    void note_rtc(const std::string& rtc) {   // mac5: the clock the run read (right after the header)
        if (f_ && !rtc.empty()) { std::fprintf(f_, "rtc %s\n", rtc.c_str()); std::fflush(f_); }
    }
    void note_mods(const std::string& hash, const std::string& ids) {   // modkit1: the active mod set
        if (f_ && !hash.empty()) { std::fprintf(f_, "mods %s %s\n", hash.c_str(), ids.c_str()); std::fflush(f_); }
    }
    void note_gameplay(const std::string& why) {   // tune4: the run's mods change gameplay (a tuning preset)
        if (f_ && !why.empty()) { std::fprintf(f_, "gameplay %s\n", why.c_str()); std::fflush(f_); }
    }
    void note_tune(const std::string& line) {   // tune4: one tuning edit (b3tune::event_line)
        if (f_) { std::fprintf(f_, "%s\n", line.c_str()); std::fflush(f_); }
    }
    void note_rumble(std::uint64_t vblank, bool on) {   // rumble1: acknowledging from this VBlank on (or not)
        if (f_) { std::fprintf(f_, "rumble %llu %d\n", static_cast<unsigned long long>(vblank), on ? 1 : 0); std::fflush(f_); }
    }
    void note_set(const std::string& line) {   // sdk2b: a code mod's setting ("set <vblank> <mod>.<key> <value>")
        if (f_ && !line.empty()) { std::fprintf(f_, "%s\n", line.c_str()); std::fflush(f_); }
    }
    void put(std::uint64_t vblank, const PadReport& s) {
        if (!f_ || (any_ && s == last_)) return;
        any_ = true;
        last_ = s;
        std::fprintf(f_, "%llu ", static_cast<unsigned long long>(vblank));
        for (unsigned char b : s) std::fprintf(f_, "%02X", b);
        std::fprintf(f_, "\n");
        ++changes_;
        std::fflush(f_);   // a crash (crash_report's _exit) still leaves the whole file
    }
    void close(std::uint64_t vblank) {
        if (!f_) return;
        std::fprintf(f_, "end %llu\n", static_cast<unsigned long long>(vblank));
        std::fclose(f_);
        f_ = nullptr;
    }
    std::uint64_t changes() const { return changes_; }

private:
    std::FILE* f_ = nullptr;
    PadReport last_{};
    bool any_ = false;
    std::uint64_t changes_ = 0;
};

class PadReplay {
public:
    // false: no file, or not a recording (the reason in error())
    bool load(const std::string& path, const PadReport& neutral) {
        cur_ = neutral;
        std::FILE* f = std::fopen(path.c_str(), "r");
        if (!f) { error_ = "cannot read " + path; return false; }
        char line[256];
        bool header = false;
        while (std::fgets(line, sizeof line, f)) {
            if (!header) {
                header = std::string(line).rfind("B3PADREC 1", 0) == 0;
                if (!header) break;
                continue;
            }
            unsigned long long v = 0;
            char hex[80] = {};
            if (std::sscanf(line, "end %llu", &v) == 1) { end_ = v; continue; }
            if (char r[64] = {}; std::sscanf(line, "rtc %63s", r) == 1) { rtc_ = r; continue; }   // mac5
            if (char h[32] = {}; std::sscanf(line, "mods %31s", h) == 1) { mods_ = h; continue; }   // modkit1
            if (int on = 0; std::sscanf(line, "rumble %llu %d", &v, &on) == 2) { rumble_.push_back({v, on != 0}); continue; }
            if (b3mod::SettingSet st; b3mod::set_from_line(line, st)) { sets_.push_back(std::move(st)); continue; }   // sdk2b
            if (std::strncmp(line, "tune ", 5) == 0) { tune_.emplace_back(line); continue; }   // tune4 (b3tune::parse_event_line)
            if (std::strncmp(line, "gameplay ", 9) == 0) continue;
            if (std::sscanf(line, "%llu %70s", &v, hex) != 2 || std::string(hex).size() != 64) continue;
            PadReport s{};
            for (int i = 0; i < 32; ++i) {
                unsigned b = 0;
                std::sscanf(hex + 2 * i, "%2x", &b);
                s[i] = static_cast<unsigned char>(b);
            }
            ev_.push_back({v, s});
        }
        std::fclose(f);
        if (!header) { error_ = path + " is not a pad recording (no B3PADREC 1 header)"; return false; }
        return true;
    }
    // The report for this VBlank (the last change at or before it)
    const PadReport& at(std::uint64_t vblank) {
        while (i_ < ev_.size() && ev_[i_].first <= vblank) cur_ = ev_[i_++].second;
        return cur_;
    }
    // rumble1: whether the recorded run acknowledged the actuator requests at this VBlank (off without "rumble" lines)
    bool rumble_at(std::uint64_t vblank) {
        while (ri_ < rumble_.size() && rumble_[ri_].first <= vblank) rumble_on_ = rumble_[ri_++].second;
        return rumble_on_;
    }
    std::size_t changes() const { return ev_.size(); }
    std::uint64_t end() const { return end_; }   // the recorded run's last VBlank (0: not recorded, e.g. a crash)
    const std::string& error() const { return error_; }
    const std::string& rtc() const { return rtc_; }   // mac5: the recorded run's clock ("" : the fixed one)
    const std::string& mods() const { return mods_; }   // modkit1: the recorded run's mod set hash ("": no mods)
    const std::vector<b3mod::SettingSet>& sets() const { return sets_; }   // sdk2b: the code mods' settings, in order
    const std::vector<std::string>& tune_lines() const { return tune_; }   // tune4: the tuning edits, as recorded

private:
    std::vector<b3mod::SettingSet> sets_;
    std::vector<std::pair<std::uint64_t, PadReport>> ev_;
    std::size_t i_ = 0;
    std::vector<std::pair<std::uint64_t, bool>> rumble_;   // rumble1
    std::vector<std::string> tune_;                         // tune4
    std::size_t ri_ = 0;
    bool rumble_on_ = false;
    PadReport cur_{};
    std::string rtc_;
    std::string mods_;
    std::uint64_t end_ = 0;
    std::string error_;
};

}  // namespace b3native

#endif
