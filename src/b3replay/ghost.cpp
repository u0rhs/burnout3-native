// ghost1: Time Attack ghosts -- the file format, the time sync and the recorder (ghost.h).
#include "ghost.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <system_error>

namespace b3replay::ghost {

namespace fs = std::filesystem;

namespace {

constexpr char kMagic[4] = {'B', '3', 'G', 'H'};
constexpr std::uint32_t kFlagTest = 1;
constexpr std::size_t kHead = 4 + 4 + 4 + 4 + 4 + 1 + 1 + 2 + 2 + 2;   // up to the two texts
constexpr std::size_t kMatBytes = 12 + 18;

struct Writer {
    std::vector<std::uint8_t> b;
    void u8(std::uint8_t v) { b.push_back(v); }
    void u16(std::uint16_t v) { u8(std::uint8_t(v)); u8(std::uint8_t(v >> 8)); }
    void u32(std::uint32_t v) { for (int k = 0; k < 4; ++k) u8(std::uint8_t(v >> (8 * k))); }
    void f32(float f) { std::uint32_t v; std::memcpy(&v, &f, 4); u32(v); }
    void i16(std::int16_t v) { u16(std::uint16_t(v)); }
    void text(const std::string& s) { b.insert(b.end(), s.begin(), s.end()); }
    void mat(const Mat34& m) {
        for (int j = 0; j < 3; ++j) f32(m.r[3][j]);
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j) i16(std::int16_t(std::lround(std::clamp(m.r[i][j], -1.99993896f, 1.99993896f) * 16384.0f)));
    }
};

struct Reader {
    const std::uint8_t* p;
    std::size_t n, at = 0;
    bool ok = true;
    bool need(std::size_t k) { if (!ok || n - at < k) ok = false; return ok; }
    std::uint8_t u8() { return need(1) ? p[at++] : 0; }
    std::uint16_t u16() { if (!need(2)) return 0; const std::uint16_t v = std::uint16_t(p[at] | (p[at + 1] << 8)); at += 2; return v; }
    std::uint32_t u32() {
        if (!need(4)) return 0;
        std::uint32_t v = 0;
        for (int k = 0; k < 4; ++k) v |= std::uint32_t(p[at + std::size_t(k)]) << (8 * k);
        at += 4;
        return v;
    }
    float f32() { const std::uint32_t v = u32(); float f; std::memcpy(&f, &v, 4); return f; }
    std::string text(std::size_t k) { if (!need(k)) return {}; std::string s(reinterpret_cast<const char*>(p + at), k); at += k; return s; }
    void mat(Mat34& m) {
        for (int j = 0; j < 3; ++j) m.r[3][j] = f32();
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j) m.r[i][j] = float(std::int16_t(u16())) / 16384.0f;
    }
};

std::uint32_t fnv1a(const std::uint8_t* p, std::size_t n) {
    std::uint32_t h = 2166136261u;
    for (std::size_t k = 0; k < n; ++k) { h ^= p[k]; h *= 16777619u; }
    return h;
}

bool fail(std::string* why, const std::string& w) { if (why) *why = w; return false; }

bool finite_mat(const Mat34& m) {
    for (const auto& r : m.r) for (float v : r) if (!std::isfinite(v)) return false;
    return true;
}

}  // namespace

std::vector<std::uint8_t> encode(const Lap& lap) {
    Writer w;
    w.b.reserve(kHead + lap.track.size() + lap.car.size() + lap.samples.size() * (8 + kMatBytes * std::size_t(2 + lap.nwheel)) + 4);
    w.text(std::string(kMagic, 4));
    w.u32(kVersion);
    w.u32(lap.test ? kFlagTest : 0);
    w.f32(lap.time);
    w.u32(std::uint32_t(lap.samples.size()));
    const int nw = std::clamp(lap.nwheel, 0, 6);
    w.u8(std::uint8_t(nw));
    w.u8(std::uint8_t(std::clamp(lap.paint, 0, 255)));
    w.u16(0);
    w.u16(std::uint16_t(std::min<std::size_t>(lap.track.size(), 0xFFFF)));
    w.u16(std::uint16_t(std::min<std::size_t>(lap.car.size(), 0xFFFF)));
    w.text(lap.track.substr(0, 0xFFFF));
    w.text(lap.car.substr(0, 0xFFFF));
    for (const Sample& s : lap.samples) {
        w.f32(s.t);
        w.mat(s.main);
        w.mat(s.body);
        for (int k = 0; k < nw; ++k) w.mat(s.wheel[k]);
        w.f32(s.wheel_speed);
    }
    w.u32(fnv1a(w.b.data(), w.b.size()));
    return w.b;
}

bool decode(const std::uint8_t* p, std::size_t n, Lap& out, std::string* why) {
    if (n < kHead + 4 || std::memcmp(p, kMagic, 4) != 0) return fail(why, "not a ghost file");
    std::uint32_t sum = 0;
    for (int k = 0; k < 4; ++k) sum |= std::uint32_t(p[n - 4 + std::size_t(k)]) << (8 * k);
    if (sum != fnv1a(p, n - 4)) return fail(why, "checksum differs (a damaged file)");
    Reader r{p, n - 4};
    r.at = 4;
    const std::uint32_t version = r.u32();
    if (version != kVersion) return fail(why, "version " + std::to_string(version) + " (this build reads " + std::to_string(kVersion) + ")");
    Lap lap;
    lap.test = (r.u32() & kFlagTest) != 0;
    lap.time = r.f32();
    const std::uint32_t count = r.u32();
    lap.nwheel = r.u8();
    lap.paint = r.u8();
    r.u16();
    const std::size_t nt = r.u16(), nc = r.u16();
    lap.track = r.text(nt);
    lap.car = r.text(nc);
    if (!r.ok || lap.nwheel > 6 || count > kMaxSamples) return fail(why, "bad header");
    const std::size_t per = 8 + kMatBytes * std::size_t(2 + lap.nwheel);
    if ((r.n - r.at) != std::size_t(count) * per) return fail(why, "wrong size for its samples");
    lap.samples.resize(count);
    float prev = -1;
    for (Sample& s : lap.samples) {
        s.t = r.f32();
        r.mat(s.main);
        r.mat(s.body);
        for (int k = 0; k < lap.nwheel; ++k) r.mat(s.wheel[k]);
        s.wheel_speed = r.f32();
        if (!std::isfinite(s.t) || !(s.t > prev) || !finite_mat(s.main)) return fail(why, "samples out of order");
        prev = s.t;
    }
    if (!r.ok || !std::isfinite(lap.time) || !(lap.time > 0)) return fail(why, "bad samples");
    out = std::move(lap);
    return true;
}

bool save(const std::string& path, const Lap& lap, std::string* why) {
    const std::vector<std::uint8_t> bytes = encode(lap);
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

bool load(const std::string& path, Lap& out, std::string* why) {
    std::ifstream f(fs::path(path), std::ios::binary);
    if (!f) return fail(why, "cannot read " + path);
    std::vector<std::uint8_t> b((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (b.size() > (std::size_t(64) << 20)) return fail(why, "too large");
    return decode(b.data(), b.size(), out, why);
}

std::string last_two(const std::string& path) {
    std::string s = path;
    for (char& c : s) if (c == '\\') c = '/';
    while (!s.empty() && s.back() == '/') s.pop_back();
    const std::size_t a = s.find_last_of('/');
    if (a == std::string::npos) return s;
    const std::size_t b = a == 0 ? std::string::npos : s.find_last_of('/', a - 1);
    return b == std::string::npos ? s : s.substr(b + 1);
}

std::string slug(const std::string& s, bool drop_extension) {
    std::string t = s;
    if (drop_extension)
        if (const std::size_t dot = t.find_last_of('.'); dot != std::string::npos && t.find('/', dot) == std::string::npos) t.resize(dot);
    std::string out;
    for (char c : t) out += std::isalnum(static_cast<unsigned char>(c)) ? char(std::toupper(static_cast<unsigned char>(c))) : '_';
    return out;
}

std::string file_name(const std::string& track, const std::string& car) { return slug(track, false) + "-" + slug(car, true) + ".b3ghost"; }

bool best_for_track(const std::string& dir, const std::string& track, Lap& out, std::string* path) {
    std::error_code ec;
    if (dir.empty() || !fs::is_directory(fs::path(dir), ec)) return false;
    const std::string prefix = slug(track, false) + "-";
    bool found = false;
    for (fs::directory_iterator it(fs::path(dir), ec), end; !ec && it != end; it.increment(ec)) {
        std::string name, full;
        try {   // (a name the narrow encoding cannot hold: not one of ours)
            name = it->path().filename().string();
            full = it->path().string();
        } catch (...) { continue; }
        if (name.size() <= prefix.size() + 8 || name.compare(0, prefix.size(), prefix) != 0 ||
            name.compare(name.size() - 8, 8, ".b3ghost") != 0)
            continue;
        Lap l;
        if (!load(full, l) || slug(l.track, false) != slug(track, false) || l.samples.size() < 2) continue;
        // a real lap beats any test lap (B3_GHOST_TEST_LAP); then the shorter time, then the name (a stable choice)
        const bool better = !found || (out.test && !l.test) ||
                            (out.test == l.test && (l.time < out.time || (l.time == out.time && path && full < *path)));
        if (better) {
            out = std::move(l);
            if (path) *path = full;
            found = true;
        }
    }
    return found;
}

bool locate(const Lap& lap, float t, At& at) {
    const auto& s = lap.samples;
    if (s.empty() || !(t >= 0) || t > s.back().t) return false;
    if (s.size() == 1 || t <= s.front().t) { at = {0, 0}; return true; }
    // the first sample after t
    const auto it = std::upper_bound(s.begin(), s.end(), t, [](float v, const Sample& x) { return v < x.t; });
    if (it == s.end()) { at = {s.size() - 2, 1}; return true; }
    const std::size_t j = std::size_t(it - s.begin());
    const float t0 = s[j - 1].t, t1 = s[j].t;
    at.i = j - 1;
    at.u = t1 > t0 ? std::clamp((t - t0) / (t1 - t0), 0.0f, 1.0f) : 0.0f;
    return true;
}

void Recorder::reset() {
    index_ = -2;
    rec_ = false;
    last_t_ = 0;
    cur_ = Lap{};
}

bool Recorder::step(const TickInput& in, Lap* done) {
    if (!in.time_attack) { reset(); return false; }
    if (index_ == -2) { index_ = in.index; return false; }   // a lap already running: its start was not seen
    auto start = [&]() {
        cur_ = Lap{};
        rec_ = index_ >= 0 && in.have_car;
        if (!rec_) return;
        cur_.car = in.car;
        cur_.paint = in.paint;
        cur_.nwheel = std::clamp(in.nwheel, 0, 6);
        Sample s = in.pose;
        s.t = 0;
        cur_.samples.push_back(s);
        last_t_ = 0;
    };
    if (in.index != index_) {
        const bool next = index_ >= 0 && in.index == (index_ + 1) % kMaxLaps;
        bool finished = false;
        if (next && rec_ && in.have_car && in.car == cur_.car && in.time > last_t_ && cur_.samples.size() >= 2 && in.time - last_t_ <= 2.0f) {
            Sample s = in.pose;
            s.t = in.time;   // the game's timer at the line: the lap time
            cur_.samples.push_back(s);
            cur_.time = in.time;
            if (done) *done = cur_;
            finished = true;
        }
        index_ = in.index;
        start();
        return finished;
    }
    if (!rec_ || !in.have_car) return false;
    if (in.time + 0.001f < last_t_ || in.time - last_t_ > 2.0f || in.car != cur_.car || cur_.samples.size() >= kMaxSamples) {
        rec_ = false;   // a restart / a jump / another car: this lap is not a clean one
        return false;
    }
    if (!(in.time > last_t_)) return false;   // paused, or the timer not moving yet
    Sample s = in.pose;
    s.t = in.time;
    cur_.samples.push_back(s);
    last_t_ = in.time;
    if (test_ > 0 && last_t_ >= test_) {   // B3_GHOST_TEST_LAP: the lap's first test_ seconds count as a lap
        cur_.time = last_t_;
        cur_.test = true;
        if (done) *done = cur_;
        rec_ = false;
        return true;
    }
    return false;
}

}  // namespace b3replay::ghost
