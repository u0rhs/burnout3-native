// The game's own stats from its save (src/native/save_stats.h: the layout and its decomp sources). Our own code.
#include "save_stats.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace b3native::save {
namespace {

std::uint32_t u32(const std::uint8_t* p) {
    return std::uint32_t{p[0]} | std::uint32_t{p[1]} << 8 | std::uint32_t{p[2]} << 16 | std::uint32_t{p[3]} << 24;
}
float f32(const std::uint8_t* p) {
    const std::uint32_t b = u32(p);
    float f;
    std::memcpy(&f, &b, 4);
    return f;
}

void put_utf8(std::string& s, std::uint32_t c) {
    if (c < 0x80) {
        s += char(c);
    } else if (c < 0x800) {
        s += char(0xC0 | (c >> 6));
        s += char(0x80 | (c & 0x3F));
    } else {
        s += char(0xE0 | (c >> 12));
        s += char(0x80 | ((c >> 6) & 0x3F));
        s += char(0x80 | (c & 0x3F));
    }
}

// The longest of a per-track float array (a stray NaN or negative counts as nothing)
float longest(const std::uint8_t* p) {
    float m = 0;
    for (int i = 0; i < kTracks; ++i) {
        const float v = f32(p + 4 * i);
        if (v > m && v < 1e9f) m = v;
    }
    return m;
}

MedalCounts medals(const std::uint8_t* p, int n) {
    MedalCounts m;
    for (int i = 0; i < n; ++i) {
        const auto v = static_cast<std::int8_t>(p[i]);
        if (v == 3) ++m.gold;
        else if (v == 2) ++m.silver;
        else if (v == 1) ++m.bronze;
    }
    return m;
}

int flags(const std::uint8_t* p, int n) {
    int c = 0;
    for (int i = 0; i < n; ++i) c += p[i] != 0;
    return c;
}

ProfileStats decode_profile(const std::uint8_t* p, int index) {
    ProfileStats s;
    s.index = index;
    for (int i = 0; i < 9; ++i) {
        const std::uint32_t c = std::uint32_t{p[8 + 2 * i]} | std::uint32_t{p[9 + 2 * i]} << 8;
        if (!c) break;
        put_utf8(s.name, c >= 0xD800 && c < 0xE000 ? 0xFFFD : c);
    }
    s.burnout_points = u32(p + 0x40);
    s.crash_damage = u32(p + 0x44);
    for (int i = 0; i < kCars; ++i) {
        const std::uint32_t n = u32(p + 0x48 + 4 * i);
        s.car_events += n;
        s.cars_driven += n != 0;
    }
    std::uint32_t series = 0;
    for (int i = 0; i < 7; ++i) series += u32(p + 0x154 + 4 * i);   // (the game's own sum wraps the same way)
    s.takedowns_online = u32(p + 0x200);
    s.takedowns_offline = series - s.takedowns_online;
    s.slams_offline = u32(p + 0x204);
    s.slams_online = u32(p + 0x208);
    s.longest_oncoming_m = longest(p + 0x20C);
    s.longest_drift_m = longest(p + 0x2A0);
    s.race_events_online = u32(p + 0x334);
    s.crash_events_online = u32(p + 0x338);
    s.race_events_offline = u32(p + 0x33C);
    s.crash_events_offline = u32(p + 0x340);
    s.race = medals(p + 0x386, kRaceEvents);
    s.crash = medals(p + 0x3CF, kCrashEvents);
    s.signature_takedowns = flags(p + 0x433, kSignatureTakedowns);
    s.headlines = flags(p + 0x447, kHeadlines);
    return s;
}

template <class T>
std::optional<T> fail(std::string* error, const std::string& what) {
    if (error) *error = what;
    return std::nullopt;
}

}  // namespace

std::optional<std::vector<ProfileStats>> decode_game_save(const std::uint8_t* b, std::size_t size, std::string* error) {
    using R = std::vector<ProfileStats>;
    if (size != kGameSaveBytes) return fail<R>(error, "not the game's save (" + std::to_string(size) + " bytes, not " + std::to_string(kGameSaveBytes) + ")");
    if (u32(b) != kGameSaveVersion) return fail<R>(error, "unknown save version " + std::to_string(u32(b)));
    std::uint32_t sum = 0;
    for (std::size_t o = 0; o < kChecksumOffset; o += 4) sum += u32(b + o);
    if (sum != u32(b + kChecksumOffset)) return fail<R>(error, "the save's checksum does not match (a damaged save: the game would not load it)");
    R out;
    for (int i = 0; i < kProfilesPerSave; ++i) {
        const std::uint8_t* p = b + kProfilesOffset + std::size_t(i) * kProfileBytes;
        if (p[0x49B] != 0) continue;   // an empty slot
        out.push_back(decode_profile(p, i));
    }
    return out;
}

std::optional<std::vector<ProfileStats>> decode_game_save(const Directories& dirs, std::string* error) {
    const auto game = game_save(dirs, error);
    if (!game) return std::nullopt;
    for (const CardSaveDirectory& d : *game)
        for (const CardFile& f : d.files)
            if (f.name == kGameSaveDirectory) return decode_game_save(f.content.data(), f.content.size(), error);
    return fail<std::vector<ProfileStats>>(error, std::string("no ") + kGameSaveDirectory + " file in the save");
}

std::optional<std::vector<ProfileStats>> read_profile_stats(const std::filesystem::path& p, std::string* error) {
    const auto dirs = read_any(p, error);
    if (!dirs) return std::nullopt;
    return decode_game_save(*dirs, error);
}

std::vector<Achievement> achievements(const ProfileStats& s) {
    const MedalCounts all = s.all();
    const auto race_medals = std::uint64_t(s.race.medals()), crash_medals = std::uint64_t(s.crash.medals());
    return {
        {"first_gold", "First Gold", "Win a gold medal in any World Tour event.", std::uint64_t(std::min(all.gold, 1)), 1},
        {"tour_medals", "Around the World", "Win a medal in all 173 World Tour events.", race_medals + crash_medals, kEvents},
        {"race_gold", "Podium Regular", "Gold in all 73 race events.", std::uint64_t(s.race.gold), kRaceEvents},
        {"crash_gold", "Junction Master", "Gold in all 100 crash events.", std::uint64_t(s.crash.gold), kCrashEvents},
        {"tour_gold", "Solid Gold", "Gold in every one of the 173 World Tour events.", std::uint64_t(all.gold), kEvents},
        {"signature", "Signature Collection", "Pull off all 20 signature takedowns.", std::uint64_t(s.signature_takedowns), kSignatureTakedowns},
        {"headlines", "Front Page", "Make all 10 crash headlines.", std::uint64_t(s.headlines), kHeadlines},
        {"takedowns_100", "Takedown Hunter", "Score 100 takedowns.", std::min<std::uint64_t>(s.takedowns(), 100), 100},
        {"takedowns_1000", "Takedown Legend", "Score 1,000 takedowns.", std::min<std::uint64_t>(s.takedowns(), 1000), 1000},
        {"crash_damage", "Wrecking Crew", "Cause 100,000,000 in crash damage.", std::min<std::uint64_t>(s.crash_damage, 100000000), 100000000},
        {"oncoming", "Wrong Way", "Drive 1,000 m into oncoming traffic in one go.", std::uint64_t(std::min(s.longest_oncoming_m, 1000.0f)), 1000},
        {"drift", "Sideways", "Drift 250 m in one go.", std::uint64_t(std::min(s.longest_drift_m, 250.0f)), 250},
        {"cars", "Test Driver", "Race 25 different cars.", std::uint64_t(std::min(s.cars_driven, 25)), 25},
    };
}

std::string group_digits(std::uint64_t v) {
    std::string d = std::to_string(v), out;
    for (std::size_t i = 0; i < d.size(); ++i) {
        if (i && (d.size() - i) % 3 == 0) out += ',';
        out += d[i];
    }
    return out;
}

std::string format_stats(const std::vector<ProfileStats>& profiles) {
    std::string t;
    if (profiles.empty()) return "no profiles in this save\n";
    auto row = [&t](const char* label, const std::string& v) {
        char buf[64];
        std::snprintf(buf, sizeof buf, "  %-28s ", label);
        t += buf;
        t += v;
        t += '\n';
    };
    auto of = [](std::uint64_t n, std::uint64_t d) { return group_digits(n) + " / " + group_digits(d); };
    auto metres = [](float m) {
        char buf[32];
        std::snprintf(buf, sizeof buf, "%.1f m", double(m));
        return std::string(buf);
    };
    for (const ProfileStats& s : profiles) {
        t += "profile " + std::to_string(s.index + 1) + ": " + (s.name.empty() ? std::string("(no name)") : s.name) + "\n";
        const MedalCounts all = s.all();
        row("events with a medal", of(std::uint64_t(all.medals()), kEvents));
        row("gold / silver / bronze", group_digits(std::uint64_t(all.gold)) + " / " + group_digits(std::uint64_t(all.silver)) + " / " + group_digits(std::uint64_t(all.bronze)));
        row("race events, gold", of(std::uint64_t(s.race.gold), kRaceEvents));
        row("crash events, gold", of(std::uint64_t(s.crash.gold), kCrashEvents));
        row("signature takedowns", of(std::uint64_t(s.signature_takedowns), kSignatureTakedowns));
        row("crash headlines", of(std::uint64_t(s.headlines), kHeadlines));
        row("burnout points", group_digits(s.burnout_points));
        row("crash damage", group_digits(s.crash_damage));
        row("takedowns (offline / online)", group_digits(s.takedowns_offline) + " / " + group_digits(s.takedowns_online));
        row("slams (offline / online)", group_digits(s.slams_offline) + " / " + group_digits(s.slams_online));
        row("race events played (off/on)", group_digits(s.race_events_offline) + " / " + group_digits(s.race_events_online));
        row("crash events played (off/on)", group_digits(s.crash_events_offline) + " / " + group_digits(s.crash_events_online));
        row("longest oncoming", metres(s.longest_oncoming_m));
        row("longest drift", metres(s.longest_drift_m));
        row("cars raced", of(std::uint64_t(s.cars_driven), kCars));
        const auto list = achievements(s);
        int done = 0;
        for (const Achievement& a : list) done += a.unlocked();
        t += "  achievements: " + std::to_string(done) + " of " + std::to_string(list.size()) + "\n";
        for (const Achievement& a : list)
            t += std::string("    [") + (a.unlocked() ? "x" : " ") + "] " + a.title + " -- " + a.description + " (" + of(a.progress, a.target) + ")\n";
    }
    return t;
}

}  // namespace b3native::save
