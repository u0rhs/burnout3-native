// speedrun1: the speedrun timer -- time text, best-time files, the tracker and the panel's rows (speedrun.h).
#include "speedrun.h"

#include "b3host/ps2float.h"   // b3fp::ref: the PS2 float model the native game runs with

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <system_error>

namespace b3replay::speedrun {

namespace fs = std::filesystem;

namespace {

constexpr char kMagic[4] = {'B', '3', 'S', 'R'};
constexpr std::uint32_t kMaxSplits = 64;
constexpr std::size_t kMaxMarks = 512;

std::uint32_t fnv1a(const std::uint8_t* p, std::size_t n) {
    std::uint32_t h = 2166136261u;
    for (std::size_t k = 0; k < n; ++k) { h ^= p[k]; h *= 16777619u; }
    return h;
}

bool fail(std::string* why, const std::string& w) { if (why) *why = w; return false; }

std::string slug(const std::string& s, bool drop_extension) {
    std::string t = s;
    if (drop_extension)
        if (const std::size_t dot = t.find_last_of('.'); dot != std::string::npos && t.find('/', dot) == std::string::npos) t.resize(dot);
    std::string out;
    for (char c : t) out += std::isalnum(static_cast<unsigned char>(c)) ? char(std::toupper(static_cast<unsigned char>(c))) : '_';
    return out;
}

bool is_time(float t) { return std::isfinite(t) && t != kNone && t > 0; }

}  // namespace

Mode mode_of(std::uint32_t vt) {
    switch (vt) {
    case 0x4DE980u: return Mode::kRace;         // CB3OfflineSingleRaceLogic
    case 0x4DEBC0u: return Mode::kTimeAttack;   // CB3OfflineBurningLapLogic (Time Attack, Burning Lap)
    case 0x4DECE0u: return Mode::kEliminator;   // CB3OfflineLapEliminatorLogic
    case 0x4DEE00u: return Mode::kRoadRage;     // CB3OfflineRoadRageLogic
    case 0x4DFB80u: return Mode::kRace2p;       // CB3Offline2PRaceLogic (player 1's timer)
    case 0x4DF660u: return Mode::kRoadRage2p;   // CB3Offline2PRoadRageLogic
    default: return Mode::kNone;
    }
}

const char* mode_name(Mode m) {
    switch (m) {
    case Mode::kRace: return "RACE";
    case Mode::kTimeAttack: return "TIMEATTACK";
    case Mode::kEliminator: return "ELIMINATOR";
    case Mode::kRoadRage: return "ROADRAGE";
    case Mode::kRace2p: return "RACE2P";
    case Mode::kRoadRage2p: return "ROADRAGE2P";
    default: return "";
    }
}

// The game's float arithmetic is the PS2's: the native game runs with the bit-level model of include/b3host/ps2float.h
// (B3_PS2_FLOAT=1: COP1 add / sub / mul truncate toward zero, div.s rounds to nearest, conversions truncate). The time
// text must be computed the same way to print the game's digits (26.35 s prints 00:26:34 on the game's HUD: 26 + 0.35
// truncates to 26.349998), so every operation below goes through b3fp::ref, the model itself, whatever this file's build.
namespace {
float f(std::uint32_t b) { return b3fp::flt(b); }
std::uint32_t u(float x) { return b3fp::bits(x); }
float add(float a, float b) { return f(b3fp::ref::add(u(a), u(b))); }
float sub(float a, float b) { return f(b3fp::ref::sub(u(a), u(b))); }
float mul(float a, float b) { return f(b3fp::ref::mul(u(a), u(b))); }
float div(float a, float b) { return f(b3fp::ref::fpu_div(u(a), u(b))); }
int to_int(float a) { return b3fp::ref::cvt_w_s(u(a)); }
float to_float(int i) { return f(b3fp::ref::cvt_s_w(i)); }
}  // namespace

// func_00155280, statement for statement
float game_round(float f0) {
    if (3.4028235e+38f == f0 || 0.0f == f0) return f0;
    const float r = sub(f0, mul(60.0f, to_float(to_int(div(f0, 60.0f)))));
    const float s = to_float(to_int(r));
    const int h = to_int(mul(100.0f, sub(r, s)));
    const float m = to_float(to_int(mul(0.016666668f, f0)));
    float res = mul(m, 60.0f);
    res = add(res, s);
    res = add(res, div(to_float(h % 100), 100.0f));
    return res;
}

// func_00394660's text: the rounded time split into minutes, seconds, hundredths, "%02d:%02d:%02d" (D_004C98C0)
std::string time_text(float x) {
    int m = 0, s = 0, c = 0;
    const float t = game_round(x);
    if (!(3.4028235e+38f == t) && std::isfinite(t)) {
        m = to_int(mul(0.016666668f, t));
        const float r = sub(t, mul(60.0f, to_float(to_int(div(t, 60.0f)))));
        s = to_int(r);
        c = to_int(mul(100.0f, sub(r, to_float(s)))) % 100;
    }
    char b[32];
    std::snprintf(b, sizeof b, "%02d:%02d:%02d", m, s, c);
    return b;
}

std::string delta_text(float d) {
    if (!std::isfinite(d)) return "";
    return (d < 0 ? "-" : "+") + time_text(std::fabs(d));
}

std::vector<std::uint8_t> encode(const Best& b) {
    std::vector<std::uint8_t> o;
    auto u8 = [&](std::uint8_t v) { o.push_back(v); };
    auto u16 = [&](std::uint16_t v) { u8(std::uint8_t(v)); u8(std::uint8_t(v >> 8)); };
    auto u32 = [&](std::uint32_t v) { for (int k = 0; k < 4; ++k) u8(std::uint8_t(v >> (8 * k))); };
    auto f32 = [&](float f) { std::uint32_t v; std::memcpy(&v, &f, 4); u32(v); };
    o.insert(o.end(), kMagic, kMagic + 4);
    u32(kVersion);
    u32(0);
    const std::string key = b.key.substr(0, 0xFFFF);
    u16(std::uint16_t(key.size()));
    o.insert(o.end(), key.begin(), key.end());
    f32(b.time);
    const std::uint32_t n = std::uint32_t(std::min<std::size_t>(b.splits.size(), kMaxSplits));
    u32(n);
    for (std::uint32_t k = 0; k < n; ++k) f32(b.splits[k]);
    f32(b.best_lap);
    u32(fnv1a(o.data(), o.size()));
    return o;
}

bool decode(const std::uint8_t* p, std::size_t n, Best& out, std::string* why) {
    if (n < 4 + 4 + 4 + 2 + 4 + 4 + 4 + 4) return fail(why, "too short");
    if (std::memcmp(p, kMagic, 4) != 0) return fail(why, "not a speedrun file");
    std::uint32_t sum = 0;
    for (int k = 0; k < 4; ++k) sum |= std::uint32_t(p[n - 4 + std::size_t(k)]) << (8 * k);
    if (sum != fnv1a(p, n - 4)) return fail(why, "checksum differs (a damaged file)");
    std::size_t at = 4;
    auto need = [&](std::size_t k) { return n - 4 - at >= k; };
    auto u32 = [&] { std::uint32_t v = 0; for (int k = 0; k < 4; ++k) v |= std::uint32_t(p[at + std::size_t(k)]) << (8 * k); at += 4; return v; };
    auto f32 = [&] { const std::uint32_t v = u32(); float f; std::memcpy(&f, &v, 4); return f; };
    if (!need(10)) return fail(why, "truncated");
    if (const std::uint32_t v = u32(); v != kVersion) return fail(why, "version " + std::to_string(v) + " (this build reads " + std::to_string(kVersion) + ")");
    u32();   // flags
    const std::size_t kb = std::size_t(p[at] | (p[at + 1] << 8));
    at += 2;
    if (!need(kb + 8)) return fail(why, "truncated");
    Best b;
    b.key.assign(reinterpret_cast<const char*>(p + at), kb);
    at += kb;
    b.time = f32();
    const std::uint32_t ns = u32();
    if (ns > kMaxSplits) return fail(why, "too many laps");
    if (!need(std::size_t(ns) * 4 + 4)) return fail(why, "truncated");
    for (std::uint32_t k = 0; k < ns; ++k) b.splits.push_back(f32());
    b.best_lap = f32();
    if (at != n - 4) return fail(why, "trailing bytes");
    out = std::move(b);
    return true;
}

bool save(const std::string& path, const Best& b, std::string* why) {
    const std::vector<std::uint8_t> bytes = encode(b);
    std::error_code ec;
    const fs::path target(path), tmp(path + ".tmp");
    if (target.has_parent_path()) fs::create_directories(target.parent_path(), ec);
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return fail(why, "cannot write " + tmp.string());
        f.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
        f.flush();
        if (!f) { f.close(); fs::remove(tmp, ec); return fail(why, "cannot write " + tmp.string()); }
    }
    fs::rename(tmp, target, ec);
    if (ec) {   // (a host whose rename does not replace: remove, then rename)
        std::error_code ec2;
        fs::remove(target, ec2);
        ec.clear();
        fs::rename(tmp, target, ec);
    }
    if (ec) { fs::remove(tmp, ec); return fail(why, "cannot replace " + path); }
    return true;
}

bool load(const std::string& path, Best& out, std::string* why) {
    std::ifstream f(fs::path(path), std::ios::binary);
    if (!f) return fail(why, "cannot read " + path);
    std::vector<std::uint8_t> b((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (b.size() > (std::size_t(1) << 20)) return fail(why, "too large");
    return decode(b.data(), b.size(), out, why);
}

std::string event_key(Mode m, const std::string& track, const std::string& car, int laps) {
    return std::string(mode_name(m)) + " " + track + " " + car + " " + std::to_string(laps);
}

std::string file_name(Mode m, const std::string& track, const std::string& car, int laps) {
    return std::string(mode_name(m)) + "-" + slug(track, false) + "-" + slug(car, true) + "-" + std::to_string(laps) + "L.b3split";
}

void Tracker::mark(const Mark& m) {
    marks_.push_back(m);
    if (marks_.size() > kMaxMarks) marks_.erase(marks_.begin(), marks_.begin() + std::ptrdiff_t(marks_.size() - kMaxMarks / 2));
}

bool Tracker::step(const TickIn& in, const Best* best, Result* out) {
    const ScoreState& s = in.s;
    const Mode m = s.valid ? mode_of(s.logic) : Mode::kNone;
    const float ref = best ? best->time : kNone, ref_lap = best ? best->best_lap : kNone;
    if (!in.race || m == Mode::kNone) {   // out of an event: the attempt (if any) ends
        if (phase_ != Phase::kIdle) mark({in.poll, Mark::kReset, 0, 0, kNone, kNone});
        phase_ = Phase::kIdle;
        mode_ = Mode::kNone;
        return false;
    }
    // a new attempt: the first tick of an event, another event, or the game going back (a restart: the race state back
    // below racing, the race time or the lap index back)
    const bool back = phase_ != Phase::kIdle &&
                      ((last_state_ >= 2 && s.state < 2) || (s.time < last_time_ && last_time_ != kNone && s.time != kNone) ||
                       (last_lap_ >= 0 && s.lap < 0));
    // (a track or car not known at this tick -- no player car in its vehicles -- is no change)
    const bool other = m != mode_ || s.laps != laps_total_ || (!in.track.empty() && !track_.empty() && in.track != track_) ||
                       (!in.car.empty() && !car_.empty() && in.car != car_);
    if (phase_ == Phase::kIdle || back || other) {
        if (phase_ != Phase::kIdle && std::getenv("B3_SPEEDRUN_DEBUG"))
            std::printf("[speedrun] poll %u: a new attempt (state %d -> %d, time %.3f -> %.3f, lap %d -> %d)\n", in.poll, last_state_, s.state,
                        double(last_time_), double(s.time), last_lap_, s.lap);
        mark({in.poll, Mark::kReset, 0, 0, ref, ref_lap});
        phase_ = Phase::kWaiting;
        mode_ = m;
        track_ = in.track;
        car_ = in.car;
        laps_total_ = s.laps;
        lap_count_ = 0;
        splits_.clear();
        last_state_ = s.state;
        last_lap_ = s.lap;
        last_time_ = s.time;
        if (s.state < 2) return false;   // (joined while racing: the start already given; marks from here)
    }
    if (track_.empty()) track_ = in.track;
    if (car_.empty()) car_ = in.car;
    bool gave = false;
    if (phase_ == Phase::kWaiting && s.state >= 2) {   // the start signal (CB3Score::Update: state 2, the race time running)
        mark({in.poll, Mark::kStart, 0, 0, ref, ref_lap});
        phase_ = Phase::kRunning;
    }
    if (phase_ == Phase::kRunning) {
        // a lap: the lap index moved on by one (IncrementLapCount: +1, back to 0 after 8); its time is the lap times'
        // entry of the lap that ended
        if (last_lap_ >= 0 && s.lap >= 0 && s.lap != last_lap_ && s.lap == (last_lap_ + 1) % 8) {
            ++lap_count_;
            const float lt = s.lap_times[last_lap_ & 7];
            splits_.push_back(lt);
            const float r = best && std::size_t(lap_count_) <= best->splits.size() ? best->splits[std::size_t(lap_count_ - 1)] : kNone;
            mark({in.poll, Mark::kLap, lap_count_, lt, r, ref_lap});
            if (m == Mode::kTimeAttack && s.laps == 0 && is_time(lt) && car_.size() && track_.size()) {
                if (out) *out = Result{Mark::kLap, m, track_, car_, 0, lt, {}};
                gave = true;
            }
        }
        if (s.state >= 3 && s.finish != 0) {   // the finish (func_00155A40: +0x8 the finish state, the race state 3)
            const bool timed = (s.finish == 1 || s.finish == 4) && is_time(s.time);
            mark({in.poll, timed ? Mark::kFinish : Mark::kOut, lap_count_, timed ? s.time : 0, ref, ref_lap});
            phase_ = Phase::kDone;
            if (timed && car_.size() && track_.size()) {
                if (out) *out = Result{Mark::kFinish, m, track_, car_, s.laps, s.time, splits_};
                gave = true;
            }
        }
    }
    last_state_ = s.state;
    last_lap_ = s.lap;
    last_time_ = s.time;
    return gave;
}

std::vector<Mark> Tracker::marks_at(std::uint32_t poll) const {
    std::size_t end = 0;
    while (end < marks_.size() && marks_[end].poll <= poll) ++end;
    std::size_t begin = end;
    while (begin > 0 && marks_[begin - 1].kind != Mark::kReset) --begin;
    if (begin > 0) --begin;   // the kReset itself (its best)
    return {marks_.begin() + std::ptrdiff_t(begin), marks_.begin() + std::ptrdiff_t(end)};
}

std::vector<Row> rows(int setting, Mode mode, const ScoreState& s, const std::vector<Mark>& marks) {
    std::vector<Row> out;
    if (setting <= 0 || mode == Mode::kNone || !s.valid) return out;
    float best = kNone, best_lap = kNone;
    const Mark* finish = nullptr;
    bool out_mark = false;
    std::vector<const Mark*> laps;
    for (const Mark& m : marks) {
        if (m.kind == Mark::kReset || m.kind == Mark::kStart) { best = m.ref; best_lap = m.ref_lap; }
        if (m.kind == Mark::kLap) laps.push_back(&m);
        if (m.kind == Mark::kFinish) finish = &m;
        if (m.kind == Mark::kOut) out_mark = true;
    }
    const bool ta = mode == Mode::kTimeAttack && s.laps == 0;   // Time Attack: laps without an end
    if (ta) {
        // the running lap is the main timer; the laps done; the best lap (kept, or one of this run's)
        for (const Mark* l : laps) if (is_time(l->time) && (!is_time(best_lap) || l->time < best_lap)) best_lap = l->time;
        const int n = int(laps.size()) + 1;
        const bool racing = s.state >= 2 && s.lap >= 0;
        out.push_back({"LAP " + std::to_string(n), time_text(racing ? s.lap_time : 0.0f), Row::kWhite});
        if (setting >= 2) {
            const std::size_t first = laps.size() > 4 ? laps.size() - 4 : 0;
            for (std::size_t k = laps.size(); k-- > first;) {
                const Mark* l = laps[k];
                const bool pb = is_time(l->time) && (!is_time(l->ref_lap) || l->time < l->ref_lap);
                out.push_back({"LAP " + std::to_string(l->lap), time_text(l->time), pb ? Row::kGreen : Row::kGrey});
            }
        }
        out.push_back({"BEST LAP", is_time(best_lap) ? time_text(best_lap) : "--:--:--", Row::kGold});
        return out;
    }
    // an event with an end: its time from the start signal, frozen at the finish
    if (out_mark || (s.state >= 3 && s.finish != 0 && !is_time(s.time)))
        out.push_back({"TIME", "--:--:--", Row::kRed});
    else
        out.push_back({finish ? "FINISH" : "TIME", time_text(s.state >= 1 ? s.time : 0.0f), finish ? Row::kGold : Row::kWhite});
    if (setting >= 2 && s.laps > 0) {
        const std::size_t first = laps.size() > 5 ? laps.size() - 5 : 0;
        for (std::size_t k = first; k < laps.size(); ++k) {
            const Mark* l = laps[k];
            Row::Colour c = Row::kGrey;
            if (is_time(l->ref) && is_time(l->time)) c = l->time < l->ref ? Row::kGreen : Row::kRed;
            out.push_back({"LAP " + std::to_string(l->lap) + "/" + std::to_string(s.laps), time_text(l->time), c});
        }
        if (!finish && !out_mark && s.state >= 2 && s.lap >= 0 && int(laps.size()) < s.laps)
            out.push_back({"LAP " + std::to_string(laps.size() + 1) + "/" + std::to_string(s.laps), time_text(s.lap_time), Row::kWhite});
    }
    if (finish && is_time(finish->ref)) {
        const float d = finish->time - finish->ref;
        out.push_back({d < 0 ? "NEW BEST" : "BEST", d < 0 ? delta_text(d) : time_text(finish->ref), d < 0 ? Row::kGreen : Row::kGold});
        if (d >= 0) out.push_back({"", delta_text(d), Row::kRed});
    } else if (finish) {
        out.push_back({"NEW BEST", "", Row::kGreen});
    } else {
        out.push_back({"BEST", is_time(best) ? time_text(best) : "--:--:--", Row::kGold});
    }
    return out;
}

}  // namespace b3replay::speedrun
