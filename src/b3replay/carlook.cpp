// carlook.cpp -- see carlook.h (reflect2).
#include "carlook.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <sstream>

namespace b3replay::carlook {

namespace {
using L = b3gfx::CarLook;
std::mutex g_mu;
L g_cur;
std::string g_dir;
bool g_explicit = false;

float clampf(const Param& p, float v) { return std::isfinite(v) ? std::clamp(v, p.lo, p.hi) : p.lo; }

std::string num(float v) {
    char b[32];
    std::snprintf(b, sizeof b, "%.4g", double(v));
    return b;
}

std::string trim(std::string s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.erase(s.begin());
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.pop_back();
    return s;
}
}  // namespace

const std::vector<Param>& params() {
    static const std::vector<Param> t = {
        {"strength", "STRENGTH", "The gloss layer's strength (the sky, horizon and sun reflection). 0: no gloss, the paint's colour floor stays.", 0.0f, 2.0f, 0.05f, &L::strength},
        {"fresnel_power", "FRESNEL POWER", "How fast the reflection grows toward the edges of the body. Higher: only the edges.", 0.5f, 8.0f, 0.25f, &L::fresnel_power},
        {"fresnel_floor", "FRESNEL FLOOR", "The reflection facing you (the gloss of flat panels).", 0.0f, 1.0f, 0.02f, &L::fresnel_floor},
        {"paint_gain", "PAINT GAIN", "How bright the reflection is on the paint (the clearcoat contrast).", 0.0f, 4.0f, 0.1f, &L::paint_gain},
        {"paint_darken", "PAINT DARKEN", "How much the paint darkens where it mirrors the ground (a darker lower body).", 0.0f, 1.0f, 0.05f, &L::paint_darken},
        {"glint_size", "SUN GLINT SIZE", "The size of the sun's glint.", 0.2f, 4.0f, 0.1f, &L::glint_size},
        {"glint_gain", "SUN GLINT GAIN", "How bright the sun's glint is.", 0.0f, 6.0f, 0.1f, &L::glint_gain},
        {"sky_gain", "SKY GAIN", "How much the sky's clouds show in the reflection.", 0.0f, 3.0f, 0.1f, &L::sky_gain},
        {"horizon_gain", "HORIZON GAIN", "The bright haze along the horizon line (a defined edge on roofs and bonnets).", 0.0f, 3.0f, 0.1f, &L::horizon_gain},
        {"ground_level", "GROUND LEVEL", "How bright the ground is in the reflection (below the horizon).", 0.0f, 0.5f, 0.01f, &L::ground_level},
        {"haze", "HAZE", "A uniform veil added to the whole environment. Off by default (it lifts blacks and washes colour).", 0.0f, 0.5f, 0.01f, &L::haze},
        {"glass_weight", "GLASS WEIGHT", "The reflection in glass facing you.", 0.0f, 1.0f, 0.02f, &L::glass_weight},
        {"glass_fresnel", "GLASS FRESNEL", "How much the glass reflection grows toward its edges.", 0.0f, 1.0f, 0.02f, &L::glass_fresnel},
        {"glass_gain", "GLASS GAIN", "How bright the sky is in the glass.", 0.0f, 4.0f, 0.1f, &L::glass_gain},
        {"glass_darken", "GLASS DARKEN", "How much darker the glass itself goes.", 0.0f, 1.0f, 0.05f, &L::glass_darken},
        {"rim", "GLASS RIM", "The bright rim at the glass edges.", 0.0f, 2.0f, 0.05f, &L::rim},
        {"colour_keep", "PAINT COLOUR KEEP", "1: the reflection takes the paint's own colour (never tints). 0: the sky's colour.", 0.0f, 1.0f, 0.05f, &L::colour_keep},
        {"traffic", "TRAFFIC STRENGTH", "Traffic cars' reflection against the racers'.", 0.0f, 1.0f, 0.05f, &L::traffic},
    };
    return t;
}

b3gfx::CarLook defaults() { return L{}; }
b3gfx::CarLook current() { std::lock_guard<std::mutex> l(g_mu); return g_cur; }
void set_current(const b3gfx::CarLook& in) {
    L v = in;
    for (const Param& p : params()) v.*(p.field) = clampf(p, v.*(p.field));
    std::lock_guard<std::mutex> l(g_mu);
    g_cur = v;
}
bool at_default(const b3gfx::CarLook& l, const Param& p) { return std::fabs(l.*(p.field) - L{}.*(p.field)) < 1e-6f; }

std::string to_ini(const b3gfx::CarLook& l) {
    std::string s = "# the enhanced car reflections' look (docs/car-look.md); delete a line for its default\n";
    for (const Param& p : params()) s += std::string(p.key) + " = " + num(l.*(p.field)) + "\n";
    return s;
}

b3gfx::CarLook from_ini(const std::string& text, std::vector<std::string>* warnings, const b3gfx::CarLook& base) {
    L v = base;
    std::istringstream in(text);
    std::string line;
    int n = 0;
    auto warn = [&](const std::string& w) { if (warnings) warnings->push_back("car_look line " + std::to_string(n) + ": " + w); };
    while (std::getline(in, line)) {
        ++n;
        const std::size_t hash = line.find('#');
        if (hash != std::string::npos) line.resize(hash);
        line = trim(line);
        if (line.empty()) continue;
        const std::size_t eq = line.find('=');
        if (eq == std::string::npos) { warn("no '='"); continue; }
        const std::string k = trim(line.substr(0, eq)), val = trim(line.substr(eq + 1));
        const Param* p = nullptr;
        for (const Param& q : params()) if (k == q.key) p = &q;
        if (!p) { warn("unknown key " + k); continue; }
        char* end = nullptr;
        const float f = std::strtof(val.c_str(), &end);
        if (val.empty() || *end || !std::isfinite(f)) { warn("bad number for " + k); continue; }
        if (f < p->lo || f > p->hi) warn(k + " out of range, clamped");
        v.*(p->field) = clampf(*p, f);
    }
    return v;
}

void set_user_dir(const std::string& dir) { std::lock_guard<std::mutex> l(g_mu); g_dir = dir; }
std::string ini_path() { std::lock_guard<std::mutex> l(g_mu); return g_dir.empty() ? std::string() : g_dir + "/car_look.ini"; }
std::string values_path() { std::lock_guard<std::mutex> l(g_mu); return g_dir.empty() ? std::string() : g_dir + "/car_look_values.txt"; }

bool load_file(const std::string& path, std::string* why) {
    std::ifstream f(path, std::ios::binary);
    if (!f) { if (why) *why = "cannot read " + path; return false; }
    std::stringstream ss;
    ss << f.rdbuf();
    std::vector<std::string> w;
    set_current(from_ini(ss.str(), &w));
    for (const std::string& s : w) std::printf("[b3carlook] %s: %s\n", path.c_str(), s.c_str());
    std::printf("[b3carlook] loaded %s\n", path.c_str());
    std::fflush(stdout);
    return true;
}

bool load_explicit(const std::string& path, std::string* why) {
    if (!load_file(path, why)) return false;
    std::lock_guard<std::mutex> l(g_mu);
    g_explicit = true;
    return true;
}
bool explicit_loaded() { std::lock_guard<std::mutex> l(g_mu); return g_explicit; }
void load_user(const std::string& dir) {
    set_user_dir(dir);
    if (dir.empty() || explicit_loaded()) return;
    std::string why;
    load_file(ini_path(), &why);   // (no file: nothing to say)
}

bool save_file(const std::string& path, std::string* why) {
    if (path.empty()) { if (why) *why = "no user data folder"; return false; }
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) { if (why) *why = "cannot write " + path; return false; }
    f << to_ini(current());
    return bool(f);
}

std::string code_block(const b3gfx::CarLook& l, bool all) {
    std::string s = "b3gfx::CarLook{\n";
    for (const Param& p : params()) {
        if (!all && at_default(l, p)) continue;
        s += std::string("    .") + p.key + " = " + num(l.*(p.field)) + "f,\n";
    }
    return s + "}";
}

std::string values_text(const b3gfx::CarLook& l) {
    return to_ini(l) + "\n// as code (include/b3gfx/b3gfx.h CarLook's defaults):\n" + code_block(l) + "\n";
}

}  // namespace b3replay::carlook
