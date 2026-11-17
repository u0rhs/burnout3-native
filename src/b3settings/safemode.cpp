// safemode.cpp -- see include/b3settings/safemode.h
#include "b3settings/safemode.h"

#include <cstdlib>
#include <fstream>
#include <sstream>

namespace b3settings {

std::string run_state_to_ini(const RunState& r) {
    std::ostringstream o;
    o << "# Burnout 3 native: the last run's state (safe mode after " << kSafeModeAfter << " unclean runs in a row)\n";
    o << "running = " << (r.running ? 1 : 0) << "\n";
    o << "unclean = " << r.unclean << "\n";
    return o.str();
}

RunState run_state_from_ini(std::string_view text) {
    RunState r;
    std::size_t at = 0;
    while (at < text.size()) {
        std::size_t end = text.find('\n', at);
        if (end == std::string_view::npos) end = text.size();
        std::string_view line = text.substr(at, end - at);
        at = end + 1;
        const std::size_t eq = line.find('=');
        if (line.empty() || line[0] == '#' || eq == std::string_view::npos) continue;
        auto trim = [](std::string_view s) {
            while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
            while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
            return s;
        };
        const std::string key(trim(line.substr(0, eq))), val(trim(line.substr(eq + 1)));
        char* e = nullptr;
        const long v = std::strtol(val.c_str(), &e, 10);
        if (val.empty() || !e || *e) continue;
        if (key == "running" && (v == 0 || v == 1)) r.running = v == 1;
        else if (key == "unclean" && v >= 0 && v < 1000) r.unclean = int(v);
    }
    return r;
}

RunState load_run_state(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    return run_state_from_ini(ss.str());
}

bool save_run_state(const std::string& path, const RunState& r) {
    const std::string tmp = path + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f << run_state_to_ini(r);
        if (!f) return false;
    }
    std::remove(path.c_str());   // Windows: rename does not replace
    return std::rename(tmp.c_str(), path.c_str()) == 0;
}

StartDecision start_run(const RunState& previous, bool force_safe) {
    StartDecision d;
    d.now.running = true;
    d.now.unclean = previous.running ? previous.unclean + 1 : 0;
    d.forced = force_safe;
    d.safe = force_safe || d.now.unclean >= kSafeModeAfter;
    return d;
}

Settings safe_settings(Settings s) {
    s = with_preset(s, Preset::kLow);
    s.msaa = 1;
    s.filter = Filter::kBilinear;
    s.hd_textures = false;
    s.car_detail_highest = false;
    s.fullscreen = false;
    s.vsync = true;
    s.frame_cap = 0;
    s.latency = 0;
    s.lead = -1;
    s.frames_in_flight = -1;
    s.dither = false;
    return s;
}

}  // namespace b3settings
