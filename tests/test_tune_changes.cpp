// test_tune_changes: the tuning console's CHANGED page and resets (tunenav2; docs/modding/tuning.md "What you
// changed"): each variable's default and what counts as changed (b3tune::Defaults), the resets as ordinary edits that a
// pad recording and a replay file carry and play back to the same writes, the CHANGED page's rows, the counts per tab,
// section and subsection, RESET SECTION's values, and the car page's boost use rate as a change (b3tune::car::State).
// No game values: every value and address is made up but the documented ones (the car's fields).
#include "test.h"

#include "b3tune/browse.h"
#include "b3tune/tune.h"
#include "../tools/native/pad_record.h"
#include "replay_file.h"

#include <filesystem>
#include <map>

using namespace b3tune;
using namespace b3tune::browse;

namespace {

VarInfo var_info(std::uint32_t index, const char* group, const char* name, const char* file, std::uint32_t type = 2, std::uint32_t count = 0) {
    VarInfo v;
    v.index = index;
    v.group = group;
    v.name = name;
    v.file = file;
    v.type = type;
    v.count = count;
    v.elements = type == 5 ? 3 : count >= 2 ? count : 1;
    v.key = key_of(name, group, file);
    return v;
}
const char* kCar1 = "../Export/ValueDB/VehiclePhysics/COMPCAR1.cfg";
const char* kCar4 = "../Export/ValueDB/VehiclePhysics/COMPCAR4.cfg";
const char* kScore = "../Export/ValueDB/Score.cfg";
// a small race: the player's car (with its camera), a rival, the boost bar
std::vector<VarInfo> small_race() {
    std::vector<VarInfo> v;
    std::uint32_t i = 0;
    for (const char* f : {kCar1, kCar4}) {
        v.push_back(var_info(i++, "Physics/Vehicle", "Mass (Kg)", f));
        v.push_back(var_info(i++, "Physics/Steering", "Response", f));
        v.push_back(var_info(i++, "Physics/Race Car/Misc", "Max Speed In Boost", f));
    }
    v.push_back(var_info(i++, "Camera/Follow/", "Camera Offset", "../Export/ValueDB/Camera/COMPCAR1.cfg", 5));
    v.push_back(var_info(i++, "Boost Bar", "Boost Rate (Boost Units per Second)", kScore, 2, 4));
    v.push_back(var_info(i++, "AI/Car", "Top speed mps", "../Export/ValueDB/AI/defaults.cfg"));
    return v;
}
std::size_t find_row(const Browser& b, Row::Kind k, const std::string& label) {
    for (std::size_t i = 0; i < b.rows().size(); ++i)
        if (b.rows()[i].kind == k && b.rows()[i].label == label) return i;
    return b.rows().size();
}

// a toy game for the defaults: variables in a word memory, registered (live) or not, edits written to the live ones
struct Toy {
    Registry reg;
    Overrides ov;
    Defaults def;
    std::map<std::uint32_t, std::uint32_t> mem;
    std::map<std::size_t, bool> live;
    std::vector<std::string> trace;
    std::size_t add(const char* group, const char* name, const char* file, std::uint32_t address, std::vector<std::uint32_t> words) {
        Registration r;
        r.entry_point = kAddFloat;
        r.address = address;
        r.type = Type::kFloat;
        r.count = words.size() >= 2 ? std::uint32_t(words.size()) : 0;
        r.key = key_of(name, group, file);
        r.group = group;
        r.name = name;
        r.file = file;
        const std::size_t i = reg.record(r);
        for (std::size_t k = 0; k < words.size(); ++k) mem[address + 4u * std::uint32_t(k)] = words[k];   // the game's default
        def.capture(i, reg.at(i), words.data());
        for (const auto& [el, w] : ov.of(r.key)) mem[address + 4u * el] = w;   // the session's edits again
        live[i] = true;
        return i;
    }
    std::optional<std::uint32_t> now(std::size_t i, std::uint32_t el) const {
        if (!live.at(i)) return std::nullopt;
        return mem.at(reg.at(i).address + 4u * el);
    }
    void apply(const Event& ev) {
        ov.set(ev.key, ev.element, ev.word);
        for (std::size_t i : reg.with_key(ev.key))
            if (live[i]) mem[reg.at(i).address + 4u * ev.element] = ev.word;
        char t[96];
        std::snprintf(t, sizeof t, "%llu %08X[%u] %08X", static_cast<unsigned long long>(ev.vblank), std::uint32_t(ev.key), ev.element, ev.word);
        trace.push_back(t);
    }
    std::vector<Event> resets(std::uint64_t v, std::size_t index, int element) const {
        return def.resets(reg, ov, v, index, element, [&](std::size_t i, std::uint32_t el) { return now(i, el); });
    }
    std::size_t changed_count() const {
        std::size_t n = 0;
        for (std::size_t i = 0; i < reg.size(); ++i)
            for (std::uint32_t el = 0; el < reg.at(i).elements(); ++el) n += def.changed(reg, ov, i, el, now(i, el));
        return n;
    }
};

}  // namespace

TEST(changed_tab_comes_after_your_car) {
    CHECK(std::size_t(Cat::kChanged) == std::size_t(Cat::kCar) + 1);
    CHECK(std::string(cat_info(Cat::kChanged).tab) == "CHANGED");
    Usage u;
    Browser b(&u);
    b.set_vars(small_race());
    b.next_tab(+1);
    CHECK(b.tab() == std::size_t(Cat::kChanged) && b.page() == kPageChanged && b.crumbs() == "CHANGED");
    CHECK(b.rows().size() == 1 && b.rows()[0].kind == Row::Kind::kText && b.rows()[0].label == "Nothing changed: every value is the game's own.");
    CHECK(b.tab_changed(std::size_t(Cat::kChanged)) == 0);
}

TEST(defaults_changed_and_resets) {
    Toy g;
    const float m = 1500.0f, r0 = 60.0f;
    const std::size_t mass1 = g.add("Physics/Vehicle", "Mass (Kg)", kCar1, 0x1000, {float_word(m)});
    const std::size_t rate = g.add("Boost Bar", "Boost Rate (Boost Units per Second)", kScore, 0x2000,
                                   {float_word(r0), float_word(r0 + 1), float_word(r0 + 2), float_word(r0 + 3)});
    const std::size_t ai = g.add("AI/Car", "Top speed mps", "../Export/ValueDB/AI/defaults.cfg", 0x3000, {float_word(70.0f)});
    CHECK(g.changed_count() == 0);
    // the game changing a value it was never edited: not a change
    g.mem[0x3000] = float_word(71.0f);
    CHECK(g.changed_count() == 0 && g.resets(1, Defaults::kAll, -1).empty());
    // two edits; one of them back to its default by hand: not changed (and not in a preset)
    const std::int32_t km = g.reg.at(mass1).key, kr = g.reg.at(rate).key;
    g.apply({10, km, 0, float_word(2000.0f)});
    g.apply({10, kr, 2, float_word(99.0f)});
    g.apply({10, kr, 3, float_word(5.0f)});
    g.apply({11, kr, 3, float_word(r0 + 3)});
    CHECK(g.changed_count() == 2);
    std::uint32_t cur = 0;
    CHECK(g.def.changed(g.reg, g.ov, rate, 2, g.now(rate, 2), &cur) && cur == float_word(99.0f));
    CHECK(!g.def.changed(g.reg, g.ov, rate, 3, g.now(rate, 3)) && !g.def.changed(g.reg, g.ov, ai, 0, g.now(ai, 0)));
    CHECK(g.def.kept(g.reg, g.ov).all().size() == 2);   // (mass, rate[2]: rate[3] is at its default again)
    // one element's reset: the default's word as an edit
    std::vector<Event> one = g.resets(20, rate, 2);
    CHECK(one.size() == 1 && one[0].key == kr && one[0].element == 2 && one[0].word == float_word(r0 + 2) && one[0].vblank == 20);
    CHECK(g.resets(20, rate, 0).empty());   // an element not changed: nothing
    // the race ends (not live): the mass's edit is kept, still a change, with the edit's word; it registers again at a
    // new address: the edit is written over the game's default, and still counts once per entry
    g.live[mass1] = false;
    CHECK(g.def.changed(g.reg, g.ov, mass1, 0, g.now(mass1, 0), &cur) && cur == float_word(2000.0f));
    const std::size_t mass2 = g.add("Physics/Vehicle", "Mass (Kg)", kCar1, 0x5000, {float_word(m)});
    CHECK(mass2 != mass1 && g.mem[0x5000] == float_word(2000.0f));
    // RESET ALL: one event per key and element, whatever number of entries share the key
    const std::vector<Event> all = g.resets(30, Defaults::kAll, -1);
    CHECK(all.size() == 2);
    for (const Event& ev : all) g.apply(ev);
    CHECK(g.changed_count() == 0 && g.mem[0x5000] == float_word(m) && g.mem[0x2008] == float_word(r0 + 2));
    CHECK(g.def.kept(g.reg, g.ov).empty());   // SAVE AS PRESET: nothing left to save
    CHECK(g.resets(31, Defaults::kAll, -1).empty());
}

TEST(resets_record_and_replay_exactly) {
    namespace fs = std::filesystem;
    const fs::path dir = fs::path("build") / "test_tune_changes_files";
    fs::create_directories(dir);
    // a session: edits, a single reset, an edit again, RESET ALL; each event recorded as the console's are
    auto session = [](Toy& g, auto&& source) {
        g.add("Physics/Vehicle", "Mass (Kg)", kCar1, 0x1000, {float_word(1500.0f)});
        g.add("Physics/Steering", "Response", kCar4, 0x1100, {float_word(0.5f)});
        g.add("Boost Bar", "Boost Rate (Boost Units per Second)", kScore, 0x2000, {float_word(60), float_word(61), float_word(62), float_word(63)});
        for (std::uint64_t v = 1; v <= 100; ++v) {
            for (const Event& ev : source(g, v)) g.apply(ev);
            char t[64];
            std::snprintf(t, sizeof t, "%llu %08X %08X %08X", static_cast<unsigned long long>(v), g.mem[0x1000], g.mem[0x1100], g.mem[0x2004]);
            g.trace.push_back(t);
        }
    };
    b3native::PadRecorder pr;
    CHECK(pr.open((dir / "pads.txt").string()));
    b3native::replay::Recorder rr(b3native::replay::Report{});
    std::size_t resets = 0;
    Toy live;
    session(live, [&](Toy& g, std::uint64_t v) {
        std::vector<Event> evs;
        const std::int32_t km = g.reg.at(0).key, ks = g.reg.at(1).key, kr = g.reg.at(2).key;
        if (v == 10) evs = {{v, km, 0, float_word(1800.0f)}, {v, kr, 1, float_word(90.0f)}};
        if (v == 20) evs = {{v, ks, 0, float_word(0.9f)}};
        if (v == 30) evs = g.resets(v, 2, -1);                  // the boost rate's reset (R on it)
        if (v == 40) evs = {{v, kr, 1, float_word(10.0f)}};
        if (v == 60) evs = g.resets(v, Defaults::kAll, -1);    // RESET ALL
        if (v == 30 || v == 60) resets += evs.size();
        for (const Event& ev : evs) {
            pr.note_tune(event_line(ev, "x"));
            rr.event(ev.vblank, "tune", event_value(ev));
        }
        return evs;
    });
    pr.close(100);
    CHECK(resets == 1 + 3 && live.changed_count() == 0);
    // the pad recording
    b3native::PadReplay back;
    CHECK(back.load((dir / "pads.txt").string(), b3native::PadReport{}));
    CHECK(back.tune_lines().size() == 3 + 1 + 1 + 3);
    EventQueue q;
    for (const std::string& l : back.tune_lines()) {
        Event ev;
        CHECK(parse_event_line(l, ev));
        q.add(ev);
    }
    Toy pad;
    session(pad, [&](Toy&, std::uint64_t v) { return q.take(v); });
    CHECK(pad.trace == live.trace);
    // the replay file
    b3native::replay::Replay& h = rr.replay();
    h.vblanks = 100;
    h.version = "0.0.0-test";
    std::string why;
    const std::optional<b3native::replay::Replay> dec = b3native::replay::decode(b3native::replay::encode(h), &why);
    CHECK(dec.has_value());
    if (dec) {
        b3native::replay::Player player(*dec);
        Toy file;
        session(file, [&](Toy&, std::uint64_t v) {
            std::vector<Event> evs;
            for (const b3native::replay::SettingOp* op : player.settings(v)) {
                Event ev;
                CHECK(op->key == "tune" && parse_event_value(op->value, ev));
                ev.vblank = v;
                evs.push_back(ev);
            }
            return evs;
        });
        CHECK(file.trace == live.trace);
    }
    fs::remove_all(dir);
}

TEST(changed_page_lists_counts_and_jumps) {
    Usage u;
    Browser b(&u);
    b.set_vars(small_race());
    const Tree& t = b.tree();
    const long mass1 = t.find("Physics/Vehicle", "Mass (Kg)", "COMPCAR1"), resp4 = t.find("Physics/Steering", "Response", "COMPCAR4");
    const long rate = t.find("Boost Bar", "Boost Rate (Boost Units per Second)");
    CHECK(mass1 >= 0 && resp4 >= 0 && rate >= 0);
    std::vector<Change> cs;
    for (const auto& [var, el] : {std::pair<long, std::uint32_t>{mass1, 0}, {resp4, 0}, {rate, 2}}) {
        Change c;
        c.item = {std::uint32_t(var), el};
        cs.push_back(c);
    }
    Change gone;   // a variable of an earlier race (not in the tree)
    gone.item = {99, 0};
    gone.live = false;
    gone.label = "Torque - COMPCAR9";
    cs.push_back(gone);
    Change freeze;
    freeze.car = int(car::kFreezeDamage);
    cs.push_back(freeze);
    b.set_changes(cs);
    // the counts: per tab (the strip), section, subsection; YOUR CAR its car row and the model's tuning it shows
    CHECK(b.tab_changed(std::size_t(Cat::kChanged)) == 5);
    CHECK(b.tab_changed(std::size_t(Cat::kHandling)) == 2 && b.tab_changed(std::size_t(Cat::kBoost)) == 1);
    CHECK(b.tab_changed(std::size_t(Cat::kEngine)) == 0 && b.tab_changed(std::size_t(Cat::kMostUsed)) == 0);
    CHECK(b.tab_changed(std::size_t(Cat::kCar)) == 2);   // freeze damage, the player's mass
    b.set_tab(std::size_t(Cat::kHandling));
    const std::size_t steer = find_row(b, Row::Kind::kNode, "Steering"), mass = find_row(b, Row::Kind::kNode, "Mass");
    CHECK(steer < b.rows().size() && mass < b.rows().size());
    CHECK(b.changed_count(b.rows()[steer].node) == 1 && b.changed_count(b.rows()[mass].node) == 1);
    b.set_cursor(steer);
    CHECK(b.enter());
    const std::size_t rival = find_row(b, Row::Kind::kNode, "COMPCAR4"), mine = find_row(b, Row::Kind::kNode, "COMPCAR1 (your car)");
    CHECK(rival < b.rows().size() && mine < b.rows().size());
    CHECK(b.changed_count(b.rows()[rival].node) == 1 && b.changed_count(b.rows()[mine].node) == 0);
    // RESET SECTION: the page's changed values, its subsections included
    CHECK(b.section_changes().size() == 1 && b.section_changes()[0].item.var == std::uint32_t(resp4));
    b.back();
    CHECK(b.section_changes().size() == 2);   // HANDLING's top: the whole tab
    b.set_tab(std::size_t(Cat::kEngine));
    CHECK(b.section_changes().empty());
    b.set_tab(std::size_t(Cat::kCar));
    CHECK(b.section_changes().size() == 2);   // freeze damage and the mass shown there
    // the CHANGED page: RESET ALL, then YOUR CAR's, then each tab's in the tabs' order, then those not in this race
    b.set_tab(std::size_t(Cat::kChanged));
    const std::vector<Row>& rs = b.rows();
    CHECK(rs.size() == 10);
    if (rs.size() == 10) {
        CHECK(rs[0].kind == Row::Kind::kAction && rs[0].action == kActResetAll);
        CHECK(rs[1].kind == Row::Kind::kText && rs[1].label == "YOUR CAR (1)");
        CHECK(rs[2].kind == Row::Kind::kCar && rs[2].car == int(car::kFreezeDamage) && rs[2].elsewhere);
        CHECK(rs[3].kind == Row::Kind::kText && rs[3].label == "HANDLING (2)");
        CHECK(rs[4].kind == Row::Kind::kVar && rs[4].item.var == std::uint32_t(resp4) && rs[4].elsewhere);   // STEERING before MASS
        CHECK(rs[5].kind == Row::Kind::kVar && rs[5].item.var == std::uint32_t(mass1));
        CHECK(rs[6].label == "BOOST & SCORE (1)" && rs[7].item.var == std::uint32_t(rate) && rs[7].item.element == 2);
        CHECK(rs[8].label == "Not in this race (kept for when it registers again)");
        CHECK(rs[9].kind == Row::Kind::kVar && rs[9].label == "Torque - COMPCAR9" && rs[9].item.var == 99);
    }
    CHECK(b.section_changes().size() == 5 && b.current() && b.current()->kind == Row::Kind::kAction);
    // ENTER on a value goes to its place; on a car row to the YOUR CAR page's row
    b.set_cursor(7);
    CHECK(b.enter() && b.tab() == std::size_t(Cat::kBoost) && b.current()->item.element == 2);
    b.set_tab(std::size_t(Cat::kChanged));
    b.set_cursor(2);
    CHECK(b.enter() && b.page() == kPageCar && b.current() && b.current()->kind == Row::Kind::kCar && b.current()->car == int(car::kFreezeDamage));
    // the same list again: nothing made again (the cursor stays); a shorter one: the page follows
    b.set_tab(std::size_t(Cat::kChanged));
    b.set_cursor(4);
    b.set_changes(cs);
    CHECK(b.cursor() == 4);
    cs.erase(cs.begin());
    b.set_changes(cs);
    CHECK(b.tab_changed(std::size_t(Cat::kHandling)) == 1 && b.tab_changed(std::size_t(Cat::kChanged)) == 4);
    // a new tree (the next race): the counts again
    b.set_vars(small_race());
    CHECK(b.tab_changed(std::size_t(Cat::kHandling)) == 1);
    b.set_changes({});
    CHECK(b.rows().size() == 1 && b.rows()[0].label == "Nothing changed: every value is the game's own.");
    CHECK(b.tab_changed(std::size_t(Cat::kCar)) == 0);
}

TEST(boost_use_rate_is_a_change_while_the_cars) {
    struct Mem {
        std::map<std::uint32_t, std::uint32_t> w;
        std::map<std::uint32_t, std::uint8_t> b;
        std::uint32_t load32(std::uint32_t a) const { const auto it = w.find(a); return it == w.end() ? 0u : it->second; }
        std::uint8_t load8(std::uint32_t a) const { const auto it = b.find(a); return it == b.end() ? 0u : it->second; }
        void store32(std::uint32_t a, std::uint32_t v) { w[a] = v; }
        void store8(std::uint32_t a, std::uint8_t v) { b[a] = v; }
    } m;
    m.store32(car::kRaceCarCount, 4);
    m.store32(car::kRaceCarList, car::kPlayerCar);
    const std::uint32_t at = car::kPlayerCar + car::kBoostRateOff;
    m.store32(at, float_word(60.0f));
    car::State s;
    bool wrote = false;
    car::apply(s, m, car::key_of(car::kBoostRate), float_word(75.0f), wrote);
    CHECK(wrote && s.rate_set && s.rate_game == 60.0f && m.load32(at) == float_word(75.0f));
    car::apply(s, m, car::key_of(car::kBoostRate), float_word(80.0f), wrote);
    CHECK(s.rate_set && s.rate_game == 60.0f);   // the game's rate is the one before the first change
    car::hold(s, m);
    CHECK(s.rate_set && m.load32(at) == float_word(80.0f));   // read only: hold writes no rate
    car::apply(s, m, car::key_of(car::kBoostRate), float_word(60.0f), wrote);
    CHECK(!s.rate_set);   // back to the game's (a reset)
    car::apply(s, m, car::key_of(car::kBoostRate), float_word(70.0f), wrote);
    m.store32(at, float_word(90.0f));   // the game sets its own (the bar grew)
    car::hold(s, m);
    CHECK(!s.rate_set && m.load32(at) == float_word(90.0f));
    car::apply(s, m, car::key_of(car::kBoostRate), float_word(70.0f), wrote);
    CHECK(s.rate_set && s.rate_game == 90.0f);
    m.store32(car::kRaceCarCount, 0);   // the race ends
    car::hold(s, m);
    CHECK(!s.rate_set);
}

TEST_MAIN("test_tune_changes: the tuning console's CHANGED page, defaults and resets (b3tune::Defaults, browse.h)")
