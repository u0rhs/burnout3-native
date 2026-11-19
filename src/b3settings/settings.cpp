// settings1: values, presets, ini, HSET (include/b3settings/settings.h).
#include "b3settings/settings.h"

#include <cctype>
#include <algorithm>
#include <iterator>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

namespace b3settings {

namespace {

// Every key, in file order: name, then how to read / write it.
struct Key {
    const char* name;
    bool Settings::* flag;
    int Settings::* num;
    int lo, hi;
};

const Key kKeys[] = {
    {"fullscreen", &Settings::fullscreen, nullptr, 0, 0},
    {"vsync", &Settings::vsync, nullptr, 0, 0},
    {"frame_cap", nullptr, &Settings::frame_cap, 0, 1000},
    {"interpolation", &Settings::interpolation, nullptr, 0, 0},
    {"hud_wide", &Settings::hud_wide, nullptr, 0, 0},
    {"fe_layout", nullptr, &Settings::fe_layout, 0, 2},
    {"movie_fill", &Settings::movie_fill, nullptr, 0, 0},
    {"split_layout", nullptr, &Settings::split_layout, 0, 1},             // splitlayout1 (the split screen's layout)
    {"pause_unfocused", &Settings::pause_unfocused, nullptr, 0, 0},
    {"fast_loading", &Settings::fast_loading, nullptr, 0, 0},   // fastload1
    {"hide_save_screens", &Settings::hide_save_screens, nullptr, 0, 0},   // autosave1
    {"latency", nullptr, &Settings::latency, 0, 3},
    {"lead", nullptr, &Settings::lead, -1, 100},
    {"frames_in_flight", nullptr, &Settings::frames_in_flight, -1, 3},
    {"msaa", nullptr, &Settings::msaa, 1, 8},
    {"render_scale", nullptr, &Settings::render_scale, 50, 100},     // effects2
    {"upscaling", nullptr, &Settings::upscaling, 0, 1},              // 1.1
    {"dither", &Settings::dither, nullptr, 0, 0},
    {"hd_textures", &Settings::hd_textures, nullptr, 0, 0},
    {"car_detail_highest", &Settings::car_detail_highest, nullptr, 0, 0},
    {"car_reflections", nullptr, &Settings::car_reflections, 0, 2},   // reflect1
    {"shadows", &Settings::shadows, nullptr, 0, 0},
    {"skid_marks", &Settings::skid_marks, nullptr, 0, 0},
    {"specular", &Settings::specular, nullptr, 0, 0},
    {"sun", &Settings::sun, nullptr, 0, 0},
    {"coronas", &Settings::coronas, nullptr, 0, 0},
    {"whole_track", &Settings::whole_track, nullptr, 0, 0},
    {"motion_blur", &Settings::motion_blur, nullptr, 0, 0},
    {"radial_blur", nullptr, &Settings::radial_blur, 0, 100},
    {"bloom", &Settings::bloom, nullptr, 0, 0},
    {"bloom_level", nullptr, &Settings::bloom_level, 1, 2},   // bloom2
    {"post_pc", &Settings::post_pc, nullptr, 0, 0},
    {"sparks", nullptr, &Settings::sparks, 0, 3},                           // effects2 (levels; true / false still read)
    {"scrapes", &Settings::scrapes, nullptr, 0, 0},
    {"popcorn", nullptr, &Settings::popcorn, 0, 3},
    {"damage", &Settings::damage, nullptr, 0, 0},
    {"glass_damage", &Settings::glass_damage, nullptr, 0, 0},
    {"fov", nullptr, &Settings::fov, 0, kFovMax},
    {"fov_correction", &Settings::fov_correction, nullptr, 0, 0},
    {"camera_distance", nullptr, &Settings::camera_distance, 90, 150},   // 1.1 (chase camera, drawn only)
    {"camera_height", nullptr, &Settings::camera_height, 0, 100},
    {"fps_view", nullptr, &Settings::fps_view, 0, 3},                     // fpsoverlay2
    {"fps_corner", nullptr, &Settings::fps_corner, 0, 3},
    {"skip_hint", &Settings::skip_hint, nullptr, 0, 0},
    {"button_prompts", nullptr, &Settings::button_prompts, 0, 6},
    {"speed_units", nullptr, &Settings::speed_units, 0, 1},   // units1
    {"date_format", nullptr, &Settings::date_format, 0, 1},   // mac5
    {"photo_row", &Settings::photo_row, nullptr, 0, 0},   // photorow1 (PHOTO MODE in the pause menu)
    {"battery_saver", &Settings::battery_saver, nullptr, 0, 0},   // effects2 (BATTERY SAVER)
    {"launcher_at_start", &Settings::launcher_at_start, nullptr, 0, 0},   // launchskip1 (the launcher's own start)
    {"playlist_mode", nullptr, &Settings::playlist_mode, 0, 2},   // custom soundtrack (remaster-features §6)
    {"custom_music_volume", nullptr, &Settings::custom_music_volume, 0, 200},   // musicvol1 (CUSTOM MUSIC VOLUME)
    {"other_music_apps", nullptr, &Settings::other_music_apps, 0, 1},   // nowplaying1 (remaster-features §6.2)
    {"screen_flashes", nullptr, &Settings::screen_flashes, 0, 1},         // accessibility
    {"hud_colour_filter", nullptr, &Settings::hud_colour_filter, 0, 3},
    {"hud_size", nullptr, &Settings::hud_size, 0, 3},                     // textsize1
    {"boost_mode", nullptr, &Settings::boost_mode, 0, 1},
    {"ghost", nullptr, &Settings::ghost, 0, 1},                           // ghost1 (Time Attack ghosts)
    {"speedrun_timer", nullptr, &Settings::speedrun_timer, 0, 2},         // speedrun1
    {"instant_restart", nullptr, &Settings::instant_restart, 0, 1},
    {"rumble", nullptr, &Settings::rumble, 0, 1},                         // rumble1 (controller rumble)
    {"rumble_strength", nullptr, &Settings::rumble_strength, 10, 100},
    {"rumble_triggers", &Settings::rumble_triggers, nullptr, 0, 0},
    {"analog_pedals", &Settings::analog_pedals, nullptr, 0, 0},   // 1.0.1
    {"adaptive_triggers", nullptr, &Settings::adaptive_triggers, 0, 1},   // pad1 (DualSense adaptive triggers)
    {"adaptive_triggers_strength", nullptr, &Settings::adaptive_triggers_strength, 10, 100},
    {"series_shoulders", nullptr, &Settings::series_shoulders, 0, 1},     // carsel1 (car series on L1 / R1)
    {"record_replays", nullptr, &Settings::record_replays, 0, 1},         // replay1 (replay files)
    {"developer_mods", nullptr, &Settings::developer_mods, 0, 1},         // tune4 (the tuning console)
};

const char* const kFilterNames[] = {"point", "bilinear", "trilinear", "aniso2", "aniso4", "aniso8", "aniso16"};

std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
    return s;
}

}  // namespace

const char* preset_name(Preset p) {
    switch (p) {
    case Preset::kPs2: return "PS2 ORIGINAL";
    case Preset::kLow: return "LOW";
    case Preset::kMedium: return "MEDIUM";
    case Preset::kHigh: return "HIGH";
    case Preset::kUltra: return "ULTRA";
    case Preset::kCustom: break;
    }
    return "CUSTOM";
}

const char* level_name(int level) {
    switch (level) {
    case kLevelOff: return "Off";
    case kLevelLow: return "Low";
    case kLevelMedium: return "Medium";
    default: return "High";
    }
}

Settings on_battery(Settings s) {
    if (!s.battery_saver) return s;
    if (s.frame_cap == 0 || s.frame_cap > 60) s.frame_cap = 60;
    s.sparks = std::min(s.sparks, int(kLevelMedium));
    s.popcorn = std::min(s.popcorn, int(kLevelMedium));
    return s;
}

std::size_t render_scale_index(int percent) {
    std::size_t best = 0;
    for (std::size_t i = 1; i < std::size(kRenderScales); ++i)
        if (std::abs(kRenderScales[i] - percent) < std::abs(kRenderScales[best] - percent)) best = i;
    return best;
}

int level_percent(int level) { return level <= kLevelOff ? 0 : level == kLevelLow ? 25 : level == kLevelMedium ? 50 : 100; }

Settings with_preset(Settings s, Preset p) {
    if (p == Preset::kCustom) return s;
    const Settings d{};   // High = the defaults
    s.msaa = d.msaa;
    s.render_scale = d.render_scale;
    s.filter = d.filter;
    s.car_detail_highest = d.car_detail_highest;
    s.car_reflections = d.car_reflections;
    s.shadows = s.skid_marks = s.specular = s.sun = s.coronas = true;
    s.whole_track = true;
    s.motion_blur = d.motion_blur;
    s.radial_blur = d.radial_blur;
    s.bloom = d.bloom;
    s.bloom_level = d.bloom_level;
    s.post_pc = d.post_pc;
    s.scrapes = true;
    s.sparks = d.sparks;
    s.popcorn = d.popcorn;
    s.damage = s.glass_damage = true;
    switch (p) {
    case Preset::kPs2:      // the PS2's look: no AA, bilinear, the GS's post buffers, the game's LODs and streamed track
        s.msaa = 1;
        s.car_reflections = 1;   // the game's own env pass (the exact original picture)
        s.bloom_level = 2;       // bloom2: the game's own bloom strength
        s.filter = Filter::kBilinear;
        s.post_pc = false;
        s.whole_track = false;
        break;
    case Preset::kLow:      // weak GPUs: no AA, no full-screen passes
        s.msaa = 1;
        s.car_reflections = 1;
        s.filter = Filter::kTrilinear;
        s.specular = false;
        s.motion_blur = false;
        s.radial_blur = 0;
        s.bloom = false;
        s.post_pc = false;
        s.render_scale = 75;         // effects2: the 3D picture at 75 % of the window
        s.sparks = kLevelLow;        // effects2: a quarter of the sparks and of the crash debris
        s.popcorn = kLevelLow;
        break;
    case Preset::kMedium:
        s.msaa = 2;
        s.filter = Filter::kAniso4;
        s.post_pc = false;
        s.sparks = kLevelMedium;     // effects2: half of them
        s.popcorn = kLevelMedium;
        break;
    case Preset::kHigh:
        break;
    case Preset::kUltra:
        s.msaa = 8;
        s.car_detail_highest = true;
        break;
    case Preset::kCustom:
        break;
    }
    return s;
}

Preset matching_preset(const Settings& s) {
    for (Preset p : {Preset::kHigh, Preset::kPs2, Preset::kLow, Preset::kMedium, Preset::kUltra})
        if (with_preset(s, p) == s) return p;
    return Preset::kCustom;
}

std::string to_ini(const Settings& s) {
    std::string out = "# Burnout 3 port: PC settings (settings1, docs/pc-settings.md). Drawn-only and host-only values.\n";
    out += "version = 1\n";
    for (const Key& k : kKeys) {
        out += k.name;
        out += " = ";
        if (k.flag) out += (s.*(k.flag)) ? "true" : "false";
        else out += std::to_string(s.*(k.num));
        out += '\n';
        if (std::strcmp(k.name, "msaa") == 0) {
            out += "filter = ";
            out += kFilterNames[static_cast<int>(s.filter)];
            out += '\n';
        }
    }
    out += "hd_pack_dir = ";   // mac5: a path (the rest of the line, as written)
    out += s.hd_pack_dir;
    out += '\n';
    out += "music_folder = ";   // a path, as hd_pack_dir
    out += s.music_folder;
    out += '\n';
    out += "language = ";   // 1.0.1: a disc's language code, empty for English
    out += s.language;
    out += '\n';
    return out;
}

Settings from_ini(std::string_view text, std::vector<std::string>* warnings) {
    Settings s;
    bool have_fps_view = false;   // fpsoverlay2: `fps_view` wins over the older `fps_counter`, whatever their order
    auto warn = [&](const std::string& w) { if (warnings) warnings->push_back(w); };
    while (!text.empty()) {
        const std::size_t nl = text.find('\n');
        std::string_view line = trim(text.substr(0, nl));
        text = nl == std::string_view::npos ? std::string_view{} : text.substr(nl + 1);
        if (line.empty() || line.front() == '#' || line.front() == ';' || line.front() == '[') continue;
        const std::size_t eq = line.find('=');
        if (eq == std::string_view::npos) { warn("no '=': " + std::string(line)); continue; }
        const std::string_view key = trim(line.substr(0, eq)), val = trim(line.substr(eq + 1));
        if (key == "version") continue;
        if (key == "hd_pack_dir") {   // mac5: a path, not a number
            if (val.find_first_of("\r\n") != std::string_view::npos) warn("hd_pack_dir: not one line");
            else s.hd_pack_dir = std::string(val);
            continue;
        }
        if (key == "music_folder") {   // a path, not a number
            if (val.find_first_of("\r\n") != std::string_view::npos) warn("music_folder: not one line");
            else s.music_folder = std::string(val);
            continue;
        }
        if (key == "language") {   // 1.0.1: two letters (a disc's GLOBAL<code>.BIN) or empty
            const bool ok = val.empty() || (val.size() == 2 && std::isalpha(static_cast<unsigned char>(val[0])) && std::isalpha(static_cast<unsigned char>(val[1])));
            if (!ok) warn("language: " + std::string(val));
            else {
                s.language = std::string(val);
                for (char& c : s.language) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            }
            continue;
        }
        if (key == "fps_counter") {   // before the views: on = the standard view
            if (val == "true" || val == "1" || val == "on") { if (!have_fps_view) s.fps_view = 2; }
            else if (val == "false" || val == "0" || val == "off") { if (!have_fps_view) s.fps_view = 0; }
            else warn("fps_counter: " + std::string(val));
            continue;
        }
        if (key == "fps_view") have_fps_view = true;
        if (key == "filter") {
            bool ok = false;
            for (int i = 0; i < 7; ++i)
                if (val == kFilterNames[i]) { s.filter = static_cast<Filter>(i); ok = true; }
            if (!ok) warn("filter: " + std::string(val));
            continue;
        }
        const Key* found = nullptr;
        for (const Key& k : kKeys) if (key == k.name) found = &k;
        if (!found) { warn("unknown key " + std::string(key)); continue; }
        if (found->flag) {
            if (val == "true" || val == "1" || val == "on") s.*(found->flag) = true;
            else if (val == "false" || val == "0" || val == "off") s.*(found->flag) = false;
            else warn(std::string(key) + ": " + std::string(val));
        } else {
            std::string v(val);
            if (found->num == &Settings::sparks || found->num == &Settings::popcorn) {   // effects2: the old on / off rows
                if (v == "true" || v == "on") v = "3";
                else if (v == "false" || v == "off") v = "0";
            }
            char* end = nullptr;
            const long n = std::strtol(v.c_str(), &end, 10);
            if (v.empty() || *end || n < found->lo || n > found->hi) { warn(std::string(key) + ": " + v); continue; }
            s.*(found->num) = int(n);
        }
    }
    if (s.msaa != 1 && s.msaa != 2 && s.msaa != 4 && s.msaa != 8) { warn("msaa: not 1, 2, 4 or 8"); s.msaa = Settings{}.msaa; }
    if (s.fov != 0 && (s.fov < kFovMin || s.fov > kFovMax)) { warn("fov: out of range"); s.fov = 0; }
    if (s.camera_distance % 10) { warn("camera_distance: 90..150 in steps of 10"); s.camera_distance = 100; }
    if (s.camera_height % 25) { warn("camera_height: 0..100 in steps of 25"); s.camera_height = 0; }
    if (s.frames_in_flight == 0) { warn("frames_in_flight: 1..3 or -1"); s.frames_in_flight = -1; }
    return s;
}

bool load_file(const std::string& path, Settings& s, std::string* why) {
    std::ifstream f(path, std::ios::binary);
    if (!f) { if (why) *why = "no file " + path; return false; }
    std::stringstream ss;
    ss << f.rdbuf();
    std::vector<std::string> w;
    s = from_ini(ss.str(), &w);
    if (why && !w.empty()) *why = w.front();
    return true;
}

bool save_file(const std::string& path, const Settings& s, std::string* why) {
    const std::string tmp = path + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) { if (why) *why = "cannot write " + tmp; return false; }
        f << to_ini(s);
        if (!f) { if (why) *why = "write failed " + tmp; return false; }
    }
    std::remove(path.c_str());   // Windows: rename does not replace
    if (std::rename(tmp.c_str(), path.c_str()) != 0) { if (why) *why = "cannot rename to " + path; return false; }
    return true;
}

std::vector<std::uint8_t> hset_encode(const Settings& s, std::uint32_t serial) {
    const std::string body = to_ini(s);
    std::vector<std::uint8_t> out(16 + ((body.size() + 15) & ~std::size_t(15)), 0);
    const std::uint32_t head[4] = {kHsetMagic, serial, std::uint32_t(body.size()), 0};
    std::memcpy(out.data(), head, 16);
    std::memcpy(out.data() + 16, body.data(), body.size());
    if (out.size() > kHsetMax) out.resize(0);
    return out;
}

std::optional<std::uint32_t> hset_serial(const std::uint8_t* h, std::uint32_t* bytes) {
    std::uint32_t head[4];
    std::memcpy(head, h, 16);
    if (head[0] != kHsetMagic || head[2] > kHsetMax - 16) return std::nullopt;
    if (bytes) *bytes = head[2];
    return head[1];
}

float fov_tangent_scale(int fov) {
    if (fov <= 0 || fov == kGameFov) return 1.0f;
    const double half = 3.14159265358979323846 / 360.0;
    return float(std::tan(double(fov) * half) / std::tan(double(kGameFov) * half));
}

}  // namespace b3settings
