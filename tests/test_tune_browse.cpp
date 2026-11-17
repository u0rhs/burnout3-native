// test_tune_browse: the tuning console's browser (tunenav1, include/b3tune/browse.h; docs/modding/tuning.md "Finding a
// value"): the mapping of every registration the code makes (docs/modding/file-formats/tuning-registry.md, the game's
// names only) to one place, the tree of a race-like registry, the navigation, favourites and recently changed values
// kept in their ini, search, the car page's rows, and the car page's edits (b3tune::car) recorded in a pad recording
// and a replay file and played back to the same writes. No game values: every value and address is made up but the
// documented ones (the car's fields).
#include "test.h"

#include "b3tune/browse.h"
#include "b3tune/tune.h"
#include "../src/b3replay/tunelayout.h"
#include "../tools/native/pad_record.h"
#include "replay_file.h"

#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

using namespace b3tune;
using namespace b3tune::browse;

namespace {

struct DocRow {
    std::string group, name, file, type;
    std::uint32_t count = 0;
    int index = 0;   // the table's # (1 .. 705)
};
// the registry table's rows (#, group, name, type, flags / count, file)
std::vector<DocRow> doc_rows() {
    std::ifstream f("docs/modding/file-formats/tuning-registry.md");
    std::vector<DocRow> out;
    std::string l;
    while (std::getline(f, l)) {
        if (l.size() < 3 || l[0] != '|') continue;
        std::vector<std::string> c;
        std::size_t at = 1;
        while (at < l.size()) {
            const std::size_t bar = l.find(" | ", at);
            c.push_back(std::string(trim(l.substr(at, bar == std::string::npos ? std::string::npos : bar - at))));
            if (bar == std::string::npos) break;
            at = bar + 3;
        }
        if (c.size() < 10 || c[0].empty() || !std::isdigit(static_cast<unsigned char>(c[0][0]))) continue;
        DocRow r;
        r.index = std::atoi(c[0].c_str());
        r.group = c[1];
        r.name = c[2];
        r.type = c[3];
        const std::size_t slash = c[5].find('/');
        r.count = slash == std::string::npos ? 0 : std::uint32_t(std::atoi(c[5].c_str() + slash + 1));
        r.file = c[7];
        if (const std::size_t b = r.file.find(" (built"); b != std::string::npos) r.file = r.file.substr(0, b);
        out.push_back(r);
    }
    return out;
}

VarInfo vi(std::uint32_t index, const char* group, const char* name, const char* file, std::uint32_t type = 2, std::uint32_t count = 0) {
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
// a race-like registry: one racing car (the player's, with its camera), a rival, a traffic model, globals
std::vector<VarInfo> race() {
    const char* p1 = "../Export/ValueDB/VehiclePhysics/COMPCAR1.cfg";
    const char* p4 = "../Export/ValueDB/VehiclePhysics/COMPCAR4.cfg";
    const char* tr = "../Export/ValueDB/VehiclePhysics/HEVYCAR13.cfg";
    std::vector<VarInfo> v;
    std::uint32_t i = 0;
    for (const char* f : {p4, p1}) {   // the rival registers first: the player's car still comes first in its sections
        v.push_back(vi(i++, "Physics/Vehicle", "Mass (Kg)", f));
        v.push_back(vi(i++, "Physics/Steering", "Response", f));
        v.push_back(vi(i++, "Physics/Steering", "Drag Coef", f));
        v.push_back(vi(i++, "Physics/Race Car/Misc", "Max Speed In Boost", f));
        v.push_back(vi(i++, "Physics/Race Car/Misc", "Accel Multiplier", f));
        v.push_back(vi(i++, "Physics/Race Car/Misc", "Wall Collsion Penalty", f));
        v.push_back(vi(i++, "Physics/Race Car/Misc", "Downforce Coef", f));
        v.push_back(vi(i++, "Physics/Transmission/Engine", "Torque", f));
        v.push_back(vi(i++, "Physics/Suspension/Front", "Spring Force", f));
    }
    v.push_back(vi(i++, "Physics/Vehicle", "Mass (Kg)", tr));
    v.push_back(vi(i++, "Physics/Suspension/Front", "Spring Force", tr));
    v.push_back(vi(i++, "Camera/Follow/", "Camera Offset", "../Export/ValueDB/Camera/COMPCAR1.cfg", 5));
    v.push_back(vi(i++, "Crash/HandyCam/", "Damping", "../Export/ValueDB/Camera/Camera.cfg"));
    v.push_back(vi(i++, "Boost Bar", "Boost Rate (Boost Units per Second)", "../Export/ValueDB/Score.cfg", 2, 4));
    v.push_back(vi(i++, "Score/Boost/Near Miss", "Boost value per Near Miss (Boost units)", "../Export/ValueDB/Score.cfg"));
    v.push_back(vi(i++, "Tarmac", "Min Speed", "../export/ValueDB/Sound/Surface.cfg"));
    v.push_back(vi(i++, "Sound/Skids/Sample1/Variant1", "Gain", "../export/ValueDB/Sound/Skids.cfg"));
    v.push_back(vi(i++, "AI/Car", "Top speed mps", "../Export/ValueDB/AI/defaults.cfg"));
    v.push_back(vi(i++, "Bloom Parameters", "Intensity", "../export/ValueDB/TrackBloom/US_C2_V2.cfg"));
    v.push_back(vi(i++, "Mystery/Group", "Something", "../x.cfg"));
    return v;
}
const Node* child(const Tree& t, int n, const std::string& label) {
    for (int k : t.node(n).kids)
        if (t.node(k).label == label) return &t.node(k);
    return nullptr;
}

}  // namespace

TEST(glob_and_titles) {
    std::string cap;
    CHECK(glob("Physics/Suspension/*", "physics/suspension/Front", &cap) && cap == "Front");
    CHECK(glob("Sound/Skids*", "Sound/Skids/Sample1/Variant1", &cap) && cap == "/Sample1/Variant1");
    CHECK(tidy(cap) == "Sample1 / Variant1");
    CHECK(glob("*Sound/Surface.cfg", "../export/ValueDB/Sound/Surface.cfg"));
    CHECK(!glob("Physics/Drift", "Physics/Drifts"));
    CHECK(glob("*", ""));
    CHECK(file_stem("../Export/ValueDB/VehiclePhysics/COMPCAR1.cfg") == "COMPCAR1" && file_stem("../export/ValueDB/TrackBloom/.cfg").empty());
}

TEST(every_registration_has_one_place) {
    const std::vector<DocRow> rows = doc_rows();
    CHECK(rows.size() == 705);   // the table's count (705 call sites)
    std::map<Cat, std::size_t> per;
    std::set<std::string> other;
    for (const DocRow& r : rows) {
        for (bool race_car : {true, false}) {
            const Place p = place(r.group, r.name, r.file, race_car);
            CHECK(std::size_t(p.cat) >= std::size_t(Cat::kHandling) && std::size_t(p.cat) < kCats);
            CHECK(!p.section.empty());
            if (race_car) ++per[p.cat];
            if (p.cat == Cat::kOther) other.insert(r.group);
        }
    }
    std::printf("    the 705 registrations by category:");
    for (const auto& [c, n] : per) std::printf(" %s %zu;", cat_info(c).tab, n);
    std::printf("\n");
    CHECK(other.empty());   // every group the code registers has a rule
    for (const auto& [c, n] : per) CHECK(n <= rows.size() / 3);   // no dumping ground
    CHECK(per[Cat::kHandling] > 0 && per[Cat::kEngine] > 0 && per[Cat::kCrash] > 0 && per[Cat::kAudio] > 0 && per[Cat::kSurfaces] > 0);
    // a few places by name
    CHECK(place("Physics/Race Car/Misc", "Max Speed In Boost", "x/VehiclePhysics/A.cfg", true).cat == Cat::kEngine);
    CHECK(place("Physics/Vehicle", "Mass (Kg)", "x/VehiclePhysics/T.cfg", false).cat == Cat::kTraffic);
    CHECK(place("Wood", "Min Speed", "../export/ValueDB/Sound/Surface.cfg", true).section == "Wood");
    CHECK(place("Sound/Prop/Barrel/", "Gain", "x/Payload.cfg", true).sub == "Barrel");
    const Place odd = place("Mystery/Group", "X", "a.cfg", true);
    CHECK(odd.cat == Cat::kOther && odd.section == "Mystery / Group" && odd.rule == -1);
}

TEST(lists_resolve_against_the_registry) {
    const std::vector<DocRow> rows = doc_rows();
    auto known = [&](const char* g, const char* n) {
        for (const DocRow& r : rows)
            if (r.group == g && r.name == n) return true;
        return false;
    };
    for (const CarTuning& s : starter()) CHECK(known(s.group, s.name));
    for (const CarTuning& s : car_tuning()) CHECK(known(s.group, s.name));
}

// docs/modding/tuning-options.md (what each variable does) covers every registration, each under the tab, section and
// subsection the console shows it in: a table row whose last cell lists registry #s ("133, 703") must have the
// registry's name in its first cell and sit under "## <TAB>" / "### <section>" / "#### <subsection>" headings that
// place() gives for it (a per-car or per-route subsection, `{file}`, has no heading of its own). The tabs come in the
// console's order.
TEST(options_doc_covers_every_registration) {
    const std::vector<DocRow> rows = doc_rows();
    std::map<int, const DocRow*> by;
    for (const DocRow& r : rows) by[r.index] = &r;
    std::ifstream f("docs/modding/tuning-options.md");
    CHECK(f.good());
    std::map<int, int> seen;   // # -> rows naming it
    std::string l, sec, sub;
    int tab = -1, last_tab = -1, bad = 0, lineno = 0;
    auto same = [](const std::string& a, const std::string& b) { return upper(a) == upper(b); };
    while (std::getline(f, l)) {
        ++lineno;
        if (l.rfind("## ", 0) == 0) {
            tab = -1;
            for (std::size_t c = 0; c < kCats; ++c)
                if (same(l.substr(3), cat_info(Cat(c)).tab)) tab = int(c);
            if (tab >= 0) {
                CHECK(tab > last_tab);   // the console's order, each tab once
                last_tab = tab;
            }
            sec.clear();
            sub.clear();
            continue;
        }
        if (l.rfind("### ", 0) == 0) { sec = l.substr(4); sub.clear(); continue; }
        if (l.rfind("#### ", 0) == 0) { sub = l.substr(5); continue; }
        if (l.size() < 3 || l[0] != '|') continue;
        std::vector<std::string> c;   // the cells (a `|` inside a cell is not used by the doc)
        for (std::size_t at = 1; at < l.size();) {
            const std::size_t bar = l.find('|', at);
            if (bar == std::string::npos) break;
            c.push_back(std::string(trim(l.substr(at, bar - at))));
            at = bar + 1;
        }
        if (c.size() < 2 || c.back().empty()) continue;
        std::vector<int> nums;
        bool list = true;
        for (std::size_t at = 0; at < c.back().size() && list;) {
            std::size_t e = c.back().find(',', at);
            if (e == std::string::npos) e = c.back().size();
            const std::string_view n = trim(std::string_view(c.back()).substr(at, e - at));
            list = !n.empty() && std::all_of(n.begin(), n.end(), [](char ch) { return std::isdigit(static_cast<unsigned char>(ch)); });
            if (list) nums.push_back(std::atoi(std::string(n).c_str()));
            at = e + 1;
        }
        if (!list || nums.empty()) continue;
        for (int n : nums) {
            ++seen[n];
            const auto it = by.find(n);
            bool ok = it != by.end() && tab >= 0 && c[0] == it->second->name;
            if (ok) {
                bool placed = false;
                for (bool race_car : {true, false}) {
                    const Place p = place(it->second->group, it->second->name, it->second->file, race_car);
                    const bool per_file = p.rule >= 0 && std::string(rules()[std::size_t(p.rule)].sub).find("{file}") != std::string::npos;
                    placed = placed || (int(p.cat) == tab && same(p.section, sec) && (per_file || same(p.sub, sub)));
                }
                ok = placed;
            }
            if (!ok && ++bad <= 10)
                std::printf("    tuning-options.md:%d: #%d is not %s, or not under its place in the console (tab %d, \"%s\" / \"%s\")\n", lineno, n,
                            c[0].c_str(), tab, sec.c_str(), sub.c_str());
        }
    }
    CHECK(bad == 0);
    std::size_t missing = 0;
    for (const DocRow& r : rows)
        if (!seen.count(r.index) && ++missing <= 10) std::printf("    tuning-options.md: #%d %s | %s is not described\n", r.index, r.group.c_str(), r.name.c_str());
    CHECK(missing == 0);
    CHECK(last_tab >= int(Cat::kRumble));   // every tab with variables has its heading
}

TEST(tree_of_a_race) {
    Tree t;
    t.build(race());
    CHECK(t.player() == "COMPCAR1");
    // every element in exactly one place
    std::set<std::pair<std::uint32_t, std::uint32_t>> seen;
    std::size_t elements = 0;
    for (const VarInfo& v : t.vars()) elements += v.elements;
    for (const auto& [n, it] : t.all()) {
        CHECK(seen.insert({it.var, it.element}).second);
        CHECK(t.where(it.var, it.element).first == n);
    }
    CHECK(seen.size() == elements);
    // the player's car first, a rival's after; the traffic model in TRAFFIC; one subsection shown directly
    const Node* steer = child(t, t.root(Cat::kHandling), "Steering");
    CHECK(steer && steer->kids.size() == 2);
    if (steer && steer->kids.size() == 2) {
        CHECK(t.node(steer->kids[0]).label == "COMPCAR1 (your car)" && t.node(steer->kids[1]).label == "COMPCAR4");
        CHECK(t.node(steer->kids[0]).items.size() == 2);
    }
    const Node* tmass = child(t, t.root(Cat::kTraffic), "Mass - HEVYCAR13");
    CHECK(tmass && tmass->items.size() == 1);
    const Node* cam = child(t, t.root(Cat::kCamera), "Follow - COMPCAR1 (your car)");
    CHECK(cam && cam->items.size() == 3);   // a vector: x y z
    CHECK(child(t, t.root(Cat::kEngine), "Speed & braking") != nullptr);
    CHECK(child(t, t.root(Cat::kCrash), "Wall hits") != nullptr);
    CHECK(child(t, t.root(Cat::kOther), "Mystery / Group") != nullptr);
    const Node* skids = child(t, t.root(Cat::kAudio), "Skids - Sample1 / Variant1");
    CHECK(skids != nullptr);
    CHECK(t.node(t.root(Cat::kHandling)).count == 2 * 5);   // mass, steering x2, downforce, suspension: the two racing cars
    CHECK(t.path_text(steer ? steer->kids[0] : 0) == "HANDLING > STEERING > COMPCAR1 (YOUR CAR)");
}

TEST(navigation) {
    Usage u;
    Browser b(&u);
    b.set_vars(race());
    CHECK(b.tab() == 0 && b.page() == kPageCar);
    b.next_tab(-1);
    CHECK(b.tab() == kCats - 1);
    b.next_tab(+1);
    b.next_tab(+1);
    CHECK(b.tab() == std::size_t(Cat::kChanged) && b.page() == kPageChanged);   // tunenav2: CHANGED after YOUR CAR
    b.set_tab(std::size_t(Cat::kHandling));
    CHECK(b.crumbs() == "HANDLING" && b.depth() == 0);
    // open STEERING, then the player's car
    const std::vector<Row>& rs = b.rows();
    std::size_t at = rs.size();
    for (std::size_t i = 0; i < rs.size(); ++i)
        if (rs[i].kind == Row::Kind::kNode && rs[i].label == "Steering") at = i;
    CHECK(at < rs.size());
    b.set_cursor(at);
    CHECK(b.enter() && b.depth() == 1 && b.crumbs() == "HANDLING > STEERING");
    CHECK(b.enter() && b.crumbs() == "HANDLING > STEERING > COMPCAR1 (YOUR CAR)");
    CHECK(b.rows().size() == 2 && b.rows()[0].kind == Row::Kind::kVar && !b.rows()[0].elsewhere);
    b.move(+1);
    CHECK(b.cursor() == 1);
    b.move(+5);
    CHECK(b.cursor() == 1);   // stays on the last row
    CHECK(!b.enter());        // a value in its place: nothing to open
    // the tree built again (a race ends, another starts): the same place and value
    b.set_vars(race());
    CHECK(b.crumbs() == "HANDLING > STEERING > COMPCAR1 (YOUR CAR)" && b.cursor() == 1);
    CHECK(b.back() && b.back() && !b.back() && b.depth() == 0);
    // the car page: its rows, then the model's tuning (resolved to the player's file)
    b.set_tab(0);
    std::size_t cars = 0, tuning = 0;
    for (const Row& r : b.rows()) {
        cars += r.kind == Row::Kind::kCar;
        if (r.kind == Row::Kind::kVar) {
            ++tuning;
            CHECK(r.elsewhere && file_stem(b.tree().var(r.item.var)->file) == "COMPCAR1");
        }
    }
    CHECK(cars == car_rows().size() && tuning == 7);   // top speed, accel, torque, downforce, mass, response, wall
    CHECK(b.current() && b.current()->kind == Row::Kind::kCar);   // the heading line is skipped
    // a value shown away from its place: CONFIRM goes there
    for (std::size_t i = 0; i < b.rows().size(); ++i)
        if (b.rows()[i].kind == Row::Kind::kVar && b.rows()[i].label == "Top speed in boost") b.set_cursor(i);
    CHECK(b.enter() && b.tab() == std::size_t(Cat::kEngine) && b.crumbs() == "ENGINE & SPEED > SPEED & BRAKING > COMPCAR1 (YOUR CAR)");
    CHECK(b.current() && b.current()->kind == Row::Kind::kVar && b.tree().var(b.current()->item.var)->name == "Max Speed In Boost");
}

TEST(search_finds_and_jumps) {
    Usage u;
    Browser b(&u);
    b.set_vars(race());
    b.set_tab(std::size_t(Cat::kAudio));
    CHECK(b.search(std::string("MASS")).size() == 3);       // two cars and the traffic model
    CHECK(b.search(std::string("mass hevy")).size() == 1);  // every word
    CHECK(b.search(std::string("")).empty());
    b.search_open();
    for (char c : std::string("top speed")) b.search_type(c);
    CHECK(b.searching() && b.crumbs() == "SEARCH: TOP SPEED");
    CHECK(b.rows().size() == 1 && b.rows()[0].kind == Row::Kind::kVar);   // AI/Car | Top speed mps
    b.search_erase();
    CHECK(b.query() == "top spee");
    CHECK(b.enter() && !b.searching() && b.tab() == std::size_t(Cat::kAI));
    CHECK(b.tree().var(b.current()->item.var)->name == "Top speed mps");
    // a search left with BACK: the place before it
    b.set_tab(std::size_t(Cat::kAudio));
    b.search_open();
    b.search_type('x');
    CHECK(b.back() && !b.searching() && b.tab() == std::size_t(Cat::kAudio));
}

TEST(favourites_and_recent_persist) {
    namespace fs = std::filesystem;
    const fs::path dir = fs::path("build") / "test_tune_browse_files";
    fs::create_directories(dir);
    const std::string path = (dir / "tuning_console.ini").string();
    Usage u;
    Browser b(&u);
    b.set_vars(race());
    const Tree& t = b.tree();
    const VarInfo* top = t.var(std::uint32_t(t.find("Physics/Race Car/Misc", "Max Speed In Boost", "COMPCAR1")));
    const VarInfo* rate = t.var(std::uint32_t(t.find("Boost Bar", "Boost Rate (Boost Units per Second)")));
    CHECK(top && rate);
    if (!top || !rate) return;
    CHECK(u.toggle(ident(*top, 0)) && u.toggle(ident(*rate, 2)));
    CHECK(!u.toggle(ident(*rate, 2)) && u.toggle(ident(*rate, 2)));   // off, on again
    for (int k = 0; k < 20; ++k) u.changed(ident(*rate, std::uint32_t(k % 4)));
    u.changed(ident(*top, 0));
    CHECK(u.recent().size() == 5 && u.recent().front() == ident(*top, 0));   // 4 elements of the rate, then the newest first
    CHECK(u.save(path) && !u.dirty());
    Usage back;
    CHECK(back.load(path));
    CHECK(back.favourites() == u.favourites() && back.recent() == u.recent());
    // the MOST USED page lists them, each going to its place
    Browser b2(&back);
    b2.set_vars(race());
    b2.set_tab(std::size_t(Cat::kMostUsed));
    CHECK(b2.rows().size() == 3);
    b2.set_cursor(0);
    CHECK(b2.enter() && b2.page() == kPageFavourites && b2.rows().size() == 2);
    CHECK(b2.rows()[1].item.element == 2 && b2.rows()[1].elsewhere);
    b2.set_cursor(1);
    CHECK(b2.enter() && b2.tab() == std::size_t(Cat::kBoost) && b2.current()->item.element == 2);
    // the starter list resolves what is registered (per-car ones to the player's car)
    b2.set_tab(std::size_t(Cat::kMostUsed));
    b2.set_cursor(2);
    CHECK(b2.enter() && b2.page() == kPageStarter);
    std::size_t n = 0;
    for (const Row& r : b2.rows()) n += r.kind == Row::Kind::kVar;
    CHECK(n == 5 + 4 + 1 + 1 + 3 + 1);   // the car's five registered here, the boost rate x4, near miss, AI top speed, the camera xyz, bloom
    // a file of someone else's (comments, an unknown section, CRLF) reads
    back.parse("# x\r\n[favourites]\r\nA | B [0] @ c.cfg\r\n[other]\r\nD | E [1] @ f.cfg\r\n\r\n[recent]\r\nbad line\r\n");
    CHECK(back.favourites().size() == 1 && back.recent().empty());
    fs::remove_all(dir);
}

TEST(car_page_values_and_changes) {
    B3TuneCar c{};
    c.size = sizeof c;
    c.active = 1;
    c.speed = 40.0f;
    c.speed_factor = 2.23694f;
    c.boost = 100.0f;
    c.boost_max = 240.0f;
    c.boost_rate = 60.0f;
    c.health = 0.5f;
    CHECK(car_value(kCarSpeed, c) == "89 MPH  (40.0 M/S)");
    CHECK(car_value(int(car::kHealth), c) == "50 %");
    std::uint32_t w = 0;
    CHECK(car_change(int(car::kBoost), c, +1, 1, w) && as_float(w) == 112.0f);
    CHECK(car_change(int(car::kBoost), c, +1, 10, w) && as_float(w) == 220.0f);
    CHECK(car_change(int(car::kHealth), c, -1, 1, w) && std::fabs(as_float(w) - 0.45f) < 1e-6f);
    CHECK(car_change(int(car::kHealth), c, -1, -10, w) && std::fabs(as_float(w) - 0.495f) < 1e-6f);
    CHECK(car_change(int(car::kRepair), c, 0, 1, w) && !car_change(int(car::kRepair), c, -1, 1, w));
    CHECK(car_change(int(car::kFreezeDamage), c, 0, 1, w) && w == 1u);
    c.active = 0;
    CHECK(!car_change(int(car::kBoost), c, +1, 1, w) && car_value(int(car::kBoost), c) == "-");
    CHECK(car_change(int(car::kInfiniteBoost), c, 0, 1, w) && w == 1u);   // a toggle waits for the next race
    CHECK(!car_change(kCarSpeed, c, +1, 1, w));
}

namespace {
// guest memory for b3tune::car's writes, and a toy race: the car's damage each VBlank, a crash clearing the infinite flag
struct ToyMem {
    std::map<std::uint32_t, std::uint32_t> w;
    std::map<std::uint32_t, std::uint8_t> b;
    std::uint32_t load32(std::uint32_t a) const { const auto it = w.find(a); return it == w.end() ? 0u : it->second; }
    std::uint8_t load8(std::uint32_t a) const { const auto it = b.find(a); return it == b.end() ? 0u : it->second; }
    void store32(std::uint32_t a, std::uint32_t v) { w[a] = v; }
    void store8(std::uint32_t a, std::uint8_t v) { b[a] = v; }
    float f(std::uint32_t a) const { return as_float(load32(a)); }
};
struct ToyRace {
    ToyMem m;
    car::State s;
    std::vector<std::string> trace;
    ToyRace() {
        m.store32(car::kRaceCarCount, 4);
        m.store32(car::kRaceCarList, car::kPlayerCar);
        m.store32(car::kPlayerCar + car::kBoostMax, float_word(240.0f));
        m.store32(car::kPlayerCar + car::kBoostNow, float_word(30.0f));
        m.store32(car::kPlayerCar + car::kHealthOff, float_word(1.0f));
    }
    void vblank(std::uint64_t v, const std::vector<Event>& evs) {
        const std::uint32_t h = car::kPlayerCar + car::kHealthOff;
        m.store32(h, float_word(std::max(0.0f, m.f(h) - 0.01f)));   // the game: damage
        if (v % 50 == 0) m.store8(car::kPlayerCar + car::kInfiniteFlag, 0);   // the game: a crash clears the flag
        for (const Event& ev : evs) {
            bool wrote = false;
            trace.push_back(std::to_string(v) + " " + car::apply(s, m, ev.key, ev.word, wrote));
        }
        car::hold(s, m);
        char t[96];
        std::snprintf(t, sizeof t, "%llu h %08X b %08X i %u", static_cast<unsigned long long>(v), m.load32(h),
                      m.load32(car::kPlayerCar + car::kBoostNow), unsigned(m.load8(car::kPlayerCar + car::kInfiniteFlag)));
        trace.push_back(t);
    }
};
}  // namespace

TEST(car_edits_record_and_replay_exactly) {
    namespace fs = std::filesystem;
    const fs::path dir = fs::path("build") / "test_tune_browse_files";
    fs::create_directories(dir);
    CHECK(car::is_key(car::key_of(car::kHealth)) && !car::is_key(key_of("Mass (Kg)", "Physics/Vehicle", "x.cfg")));
    CHECK(!car::is_key(std::int32_t(car::kKeyBase | 77u)));
    const std::vector<Event> live = {{10, car::key_of(car::kFillBoost), 0, 1},
                                     {20, car::key_of(car::kFreezeDamage), 0, 1},
                                     {40, car::key_of(car::kHealth), 0, float_word(0.25f)},
                                     {60, car::key_of(car::kInfiniteBoost), 0, 1},
                                     {80, car::key_of(car::kRepair), 0, 1},
                                     {90, car::key_of(car::kFreezeDamage), 0, 0},
                                     {95, car::key_of(car::kBoost), 0, float_word(999.0f)}};   // clamped to the bar
    ToyRace rec;
    b3native::PadRecorder pr;
    CHECK(pr.open((dir / "pads.txt").string()));
    b3native::replay::Recorder rr(b3native::replay::Report{});
    for (std::uint64_t v = 1; v <= 120; ++v) {
        std::vector<Event> evs;
        for (const Event& ev : live)
            if (ev.vblank == v) {
                evs.push_back(ev);
                pr.note_tune(event_line(ev, "car"));
                rr.event(ev.vblank, "tune", event_value(ev));
            }
        rec.vblank(v, evs);
    }
    pr.close(120);
    // what the session did: boost filled, health held from 20 to 90 (with the edit to 0.25 and the repair), the flag
    // held from 60 on through the crashes at 100, the boost clamped to 240
    CHECK(rec.m.f(car::kPlayerCar + car::kBoostNow) == 240.0f);
    CHECK(rec.m.load8(car::kPlayerCar + car::kInfiniteFlag) == 1);
    CHECK(std::fabs(rec.m.f(car::kPlayerCar + car::kHealthOff) - (1.0f - 0.01f * 31)) < 1e-4f);   // held at 1.0 to 89, then 31 VBlanks of damage
    // the pad recording played back
    b3native::PadReplay back;
    CHECK(back.load((dir / "pads.txt").string(), b3native::PadReport{}));
    CHECK(back.tune_lines().size() == live.size());
    EventQueue q;
    for (const std::string& l : back.tune_lines()) {
        Event ev;
        CHECK(parse_event_line(l, ev) && car::is_key(ev.key));
        q.add(ev);
    }
    ToyRace pad;
    for (std::uint64_t v = 1; v <= 120; ++v) pad.vblank(v, q.take(v));
    CHECK(pad.trace == rec.trace);
    // the replay file
    b3native::replay::Replay& h = rr.replay();
    h.vblanks = 120;
    h.version = "0.0.0-test";
    std::string why;
    const std::optional<b3native::replay::Replay> dec = b3native::replay::decode(b3native::replay::encode(h), &why);
    CHECK(dec.has_value());
    if (!dec) return;
    b3native::replay::Player player(*dec);
    ToyRace file;
    for (std::uint64_t v = 1; v <= 120; ++v) {
        std::vector<Event> evs;
        for (const b3native::replay::SettingOp* op : player.settings(v)) {
            Event ev;
            CHECK(op->key == "tune" && parse_event_value(op->value, ev));
            ev.vblank = v;
            evs.push_back(ev);
        }
        file.vblank(v, evs);
    }
    CHECK(file.trace == rec.trace);
    // no race: nothing written, the toggle kept for the next one
    ToyRace none;
    none.m.store32(car::kRaceCarCount, 0);
    bool wrote = true;
    car::apply(none.s, none.m, car::key_of(car::kRepair), 1, wrote);
    CHECK(!wrote);
    car::apply(none.s, none.m, car::key_of(car::kFreezeDamage), 1, wrote);
    CHECK(none.s.freeze && none.s.held < 0);
    fs::remove_all(dir);
}

// tunenav3: the console's box must fit its window: no label wider than its column, the box inside the window, at every
// supported size; checked with every row the registry gives (all 705 registrations, every page of the browser) at its
// longest as the console draws it (the gutter marks, "  - <place>" on the list pages, a value with its default)
namespace {
void collect_labels(Browser& b, std::set<std::string>& out, int depth) {
    const std::size_t n = b.rows().size();
    for (std::size_t i = 0; i < n; ++i) {
        const Row r = b.rows()[i];
        out.insert(r.label);
        if (r.kind == Row::Kind::kNode && depth < 4) {
            b.set_cursor(i);
            if (b.enter()) {
                collect_labels(b, out, depth + 1);
                b.back();
            }
        }
    }
}
}  // namespace

TEST(the_box_fits_every_window) {
    namespace tl = b3replay::tunelayout;
    std::vector<VarInfo> vars;
    std::uint32_t idx = 0;
    for (const DocRow& r : doc_rows()) {
        const std::uint32_t type = r.type.rfind("vector", 0) == 0 ? 5 : r.type == "int" ? 1 : 2;
        vars.push_back(vi(idx++, r.group.c_str(), r.name.c_str(), r.file.c_str(), type, r.count));
    }
    Usage u;
    Browser b(&u);
    b.set_vars(vars);
    std::set<std::string> labels;
    for (std::size_t tab = 0; tab < kCats; ++tab) {
        b.set_tab(tab);
        collect_labels(b, labels, 0);
    }
    CHECK(labels.size() > 300);
    std::size_t longest = 0;
    std::string longest_label;
    for (const std::string& l : labels)
        if (l.size() > longest) { longest = l.size(); longest_label = l; }
    std::printf("    %zu distinct row labels, the longest (%zu characters): \"%s\"\n", labels.size(), longest, longest_label.c_str());
    // the header and footer lines at their longest (as lines_locked writes them)
    const char* wide[] = {"TUNING 15/15  < MOST USED (12)  [CHANGED (123)]  YOUR CAR >",
                          "HANDLING > STEERING > COMPCAR1 (YOUR CAR)",
                          "LEFT/RIGHT CHANGE   SHIFT X10   CTRL /10   ENTER GO THERE",
                          "SQUARE RESET   TRIANGLE FAVOURITE   R2+SQUARE SECTION   SELECT ALL",
                          "TAB/Q/E TABS   / SEARCH   BACKSPACE BACK   F9 CLOSE",
                          "RESET SECTION: 123 VALUES OF HANDLING > STEERING > COMPCAR1 (YOUR CAR)?"};
    struct Size { int w, h; };
    const Size sizes[] = {{1920, 1080}, {1440, 1080}, {1280, 960}, {2560, 1080}, {3840, 1080}, {1280, 800}, {1280, 720}, {1366, 768}, {3840, 2160}, {1024, 768}};
    const std::vector<std::string> v(labels.begin(), labels.end());
    for (const Size& sz : sizes) {
        std::size_t cut_pages = 0, pages = 0;
        for (std::size_t at = 0; at < v.size(); at += 15) {   // pages of 15 rows, each label at its longest
            std::vector<tl::Item> in;
            for (const char* w : wide) {
                tl::Item it;
                it.text = w;
                it.wide = true;
                in.push_back(it);
            }
            for (std::size_t k = at; k < std::min(v.size(), at + 15); ++k) {
                tl::Item it;
                it.text = v[k] + "  - Follow - COMPCAR1 (your car)";
                it.mark = "*+";
                it.value = "-1234.5678";
                it.def = "-1234.5678";
                in.push_back(it);
            }
            const tl::Fit f = tl::fit(in, sz.w, sz.h);
            ++pages;
            if (f.cut) ++cut_pages;
            CHECK(f.panel_w <= tl::max_panel_w(sz.w, sz.h) + 1 && f.panel_h <= tl::max_panel_h(sz.h) + 1);
            CHECK(f.m.cap >= 8.0f);   // never smaller than 8 px caps
            for (const tl::Item& it : f.items) {   // nothing wider than its place: labels in their column, lines in the box
                const float room = it.wide ? f.panel_w - 2 * f.m.pad : f.label_w;
                CHECK(b3replay::glyph::text_width(it.text, f.m.cap, f.m.st) <= room + 0.5f);
                CHECK(it.text.find('\n') == std::string::npos);
            }
        }
        std::printf("    %4dx%-4d: %zu of %zu pages need a label cut (the long \"  - place\" suffix included)\n", sz.w, sz.h, cut_pages, pages);
        CHECK(sz.w < 1280 || sz.h < 720 || cut_pages <= pages / 20 + 1);   // at 1280x720 and up a cut is the exception
    }
}

TEST_MAIN("test_tune_browse: the tuning console's browser (include/b3tune/browse.h) and the car page's edits")
