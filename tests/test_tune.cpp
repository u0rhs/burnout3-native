// test_tune: the tuning console's model (tune4, include/b3tune/tune.h; docs/modding/tuning.md) on synthetic data: the
// key, the registry's recording, ranges and steps, presets (parsing, precedence, load order), edits as events through
// a pad recording and a replay file and back, and a [tuning] mod's manifest and hash. No game data: every name, value
// and address here is made up (only the hash's documented example string is the game's).
#include "test.h"

#include "b3tune/tune.h"
#include "b3mod/mods.h"
#include "../tools/native/pad_record.h"
#include "replay_file.h"

#include <filesystem>
#include <fstream>
#include <map>

using namespace b3tune;

namespace {

Registration reg(std::uint32_t addr, Type type, const char* group, const char* name, const char* file, std::uint32_t count = 0,
                 float lo = 0, float hi = 0, std::uint32_t slot = 0) {
    Registration r;
    r.entry_point = type == Type::kFloat ? kAddFloat : type == Type::kInt ? kAddInt : kAddVector;
    r.db = 0x00400000u;
    r.slot = slot;
    r.address = addr;
    r.type = type;
    r.count = count;
    r.name = name;
    r.group = group;
    r.file = file;
    r.key = key_of(name, group, file);
    if (lo != 0 || hi != 0) {
        r.flags = 1;
        if (type == Type::kInt) { r.min_word = std::uint32_t(std::int32_t(lo)); r.max_word = std::uint32_t(std::int32_t(hi)); }
        else { r.min_word = float_word(lo); r.max_word = float_word(hi); }
    }
    return r;
}

// A toy game: variables in a word map, registered through the registry, presets applied at registration, edits by
// events at VBlanks. Its trace is what a run "did"; a replay must give the same trace.
struct Toy {
    Registry r;
    Overrides ov;
    std::map<std::uint32_t, std::uint32_t> mem;
    std::vector<Preset> presets;
    std::vector<std::string> trace;
    void registered(const Registration& g, std::uint32_t game_default) {
        for (std::uint32_t w = 0; w < (g.type == Type::kVector ? 4u : g.count >= 2 ? g.count : 1u); ++w) mem[g.address + 4 * w] = game_default;
        const std::size_t i = r.record(g);
        const Entry& e = r.at(i);
        for (const Applied& a : preset_values(presets, e)) mem[e.address + 4 * a.element] = a.word;
        for (const auto& [el, w] : ov.of(e.key)) mem[e.address + 4 * el] = w;
    }
    void apply(const Event& ev) {
        ov.set(ev.key, ev.element, ev.word);
        for (std::size_t i : r.with_key(ev.key)) mem[r.at(i).address + 4 * ev.element] = ev.word;
        trace.push_back(std::to_string(ev.vblank) + " " + event_value(ev));
    }
};

}  // namespace

TEST(hash_matches_the_documented_example) {
    // docs/modding/file-formats/vdb.md "The key": the example's key and its file record's key; "" gives -1
    CHECK_BITS(std::uint32_t(key_of("Minimum Air Distance (Metres)", "Score/Boost/Air", "../Export/ValueDB/Score.cfg")), 0x58C54868u);
    CHECK_BITS(std::uint32_t(gt_hash("../Export/ValueDB/Score.cfg")), 0x9169203Eu);
    CHECK_BITS(std::uint32_t(gt_hash("")), 0xFFFFFFFFu);
    CHECK(key_of("N", "G/", "f") == gt_hash("NG/f"));   // a group ending in '/' gets no second one
    CHECK(key_of("N", "G", "f") == gt_hash("NG/f"));
}

TEST(registry_records_in_order) {
    Registry r;
    const std::size_t a = r.record(reg(0x1000, Type::kFloat, "Grp", "Speed", "a.cfg", 0, 0, 100));
    const std::size_t b = r.record(reg(0x2000, Type::kInt, "Grp", "Count", "a.cfg", 0, 1, 9));
    const std::size_t c = r.record(reg(0x3000, Type::kVector, "Cam/", "Offset", "car1.cfg"));
    const std::size_t d = r.record(reg(0x4000, Type::kVector, "Cam/", "Offset", "car2.cfg"));
    CHECK(a == 0 && b == 1 && c == 2 && d == 3);
    // the same variable registered again (the next race): the same entry, counted, its slot updated
    Registration again = reg(0x1000, Type::kFloat, "Grp", "Speed", "a.cfg", 0, 0, 100, 7);
    CHECK(r.record(again) == 0);
    CHECK(r.at(0).registrations == 2 && r.at(0).slot == 7);
    // the same key at another address (a second car of one model): a new entry with that key
    CHECK(r.record(reg(0x5000, Type::kVector, "Cam/", "Offset", "car1.cfg")) == 4);
    CHECK(r.size() == 5 && r.calls() == 6);
    CHECK(r.with_key(key_of("Offset", "Cam/", "car1.cfg")).size() == 2);
    CHECK(r.with_label("Cam/", "Offset").size() == 3);
    CHECK(r.at(1).type == Type::kInt && r.at(1).ranged() && !r.at(2).ranged());
    CHECK(r.at(2).elements() == 3 && r.at(2).words() == 4);
    CHECK(r.at(2).element_label(1) == "Cam/ | Offset y");
    Registration dropped = reg(0x6000, Type::kFloat, "Off", "Gone", "disabled.cfg");
    dropped.dropped = true;
    r.record(dropped);
    CHECK(r.dropped_calls() == 1 && r.at(5).dropped);
}

TEST(values_format_and_parse) {
    Entry f;
    f.type = Type::kFloat;
    Entry i;
    i.type = Type::kInt;
    CHECK(format_word(f, float_word(240.0f)) == "240.0");
    CHECK(format_word(f, float_word(0.1f)) == "0.1");
    CHECK(format_word(f, float_word(1e-7f)) == "1e-07");
    CHECK(format_word(i, std::uint32_t(-5)) == "-5");
    std::uint32_t w = 0;
    CHECK(parse_word("2.5", true, w) && w == float_word(2.5f));
    CHECK(parse_word("0x3F800000", true, w) && w == 0x3F800000u);
    CHECK(parse_word("-7", false, w) && std::int32_t(w) == -7);
    std::string why;
    CHECK(!parse_word("2.5", false, w, &why) && why.find("int") != std::string::npos);
    CHECK(!parse_word("abc", true, w));
    CHECK(!parse_word("99999999999", false, w));
}

TEST(range_clamping_and_steps) {
    Registry r;
    r.record(reg(0x1000, Type::kFloat, "G", "F", "x.cfg", 0, 0.0f, 10.0f));
    r.record(reg(0x2000, Type::kInt, "G", "I", "x.cfg", 0, 1, 5));
    r.record(reg(0x3000, Type::kFloat, "G", "Free", "x.cfg"));
    r.record(reg(0x4000, Type::kInt, "G", "Wide", "x.cfg", 0, 0, 1000));
    const Entry &f = r.at(0), &i = r.at(1), &free = r.at(2), &wide = r.at(3);
    bool c = false;
    CHECK(clamp_word(f, float_word(12.0f), &c) == float_word(10.0f) && c);
    CHECK(clamp_word(f, float_word(-1.0f), &c) == float_word(0.0f) && c);
    CHECK(clamp_word(f, float_word(3.0f), &c) == float_word(3.0f) && !c);
    CHECK(clamp_word(i, 9u, &c) == 5u && c);
    CHECK(clamp_word(i, std::uint32_t(-3), &c) == 1u && c);
    CHECK(clamp_word(free, float_word(1e9f), &c) == float_word(1e9f) && !c);   // no range: as given
    // steps: a hundredth of the range, clamped at the ends; ints 1, or a hundredth of a wide range
    CHECK(step_word(f, float_word(5.0f), +1) == float_word(5.1f));
    CHECK(step_word(f, float_word(10.0f), +1) == float_word(10.0f));
    CHECK(step_word(f, float_word(0.0f), -1) == float_word(0.0f));
    CHECK(step_word(i, 5u, +1) == 5u && step_word(i, 3u, -1) == 2u);
    CHECK(step_word(wide, 500u, +1) == 510u);
    // no range: 1 % of the value's decade, so up then down comes back
    const std::uint32_t up = step_word(free, float_word(240.0f), +1);
    CHECK(up == float_word(250.0f));
    CHECK(step_word(free, up, -1) == float_word(240.0f));
    CHECK(step_word(free, float_word(0.0f), +1) == float_word(0.01f));
}

TEST(preset_parsing) {
    const Preset p = parse_preset("# a comment\n"
                                  "; another\n"
                                  "G | F = 2.5\n"
                                  "[x.cfg]\n"
                                  "G | I = 3\n"
                                  "Cam/ | Offset = (1.0, 2.0, 3.0)\n"
                                  "[]\n"
                                  "0x12345678 = 0x3F800000\n"
                                  "G | Arr[2] = 7.0\n"
                                  "@version = 2\n"
                                  "not a line\n"
                                  "G | = 1\n"
                                  "NoBar = 1\n"
                                  "G | Bad[x] = 1\n",
                                  "my-mod", "tuning/p.ini");
    CHECK(p.lines.size() == 5);
    CHECK(p.errors.size() == 4);
    CHECK(p.lines[0].target.group == "G" && p.lines[0].target.name == "F" && p.lines[0].target.file.empty());
    CHECK(p.lines[1].target.file == "x.cfg" && p.lines[1].value == "3");
    CHECK(p.lines[2].target.name == "Offset" && p.lines[2].value == "(1.0, 2.0, 3.0)");
    CHECK(p.lines[3].target.by_key && std::uint32_t(p.lines[3].target.key) == 0x12345678u && p.lines[3].target.file.empty());
    CHECK(p.lines[4].target.element == 2 && p.lines[4].line == 9);
    CHECK(p.errors[0].rfind("line 11:", 0) == 0);
}

TEST(preset_apply_order_and_precedence) {
    Registry r;
    r.record(reg(0x1000, Type::kFloat, "G", "F", "x.cfg", 0, 0.0f, 10.0f));
    r.record(reg(0x2000, Type::kFloat, "G", "F", "y.cfg", 0, 0.0f, 10.0f));
    r.record(reg(0x3000, Type::kFloat, "G", "Arr", "x.cfg", 4));
    r.record(reg(0x4000, Type::kVector, "Cam/", "Offset", "x.cfg"));
    r.record(reg(0x5000, Type::kInt, "G", "I", "x.cfg", 0, 1, 5));
    const Entry &fx = r.at(0), &fy = r.at(1), &arr = r.at(2), &vec = r.at(3), &in = r.at(4);
    // within one preset: a key line beats a [file] line beats a plain name line, the later of two equal lines wins
    const Preset a = parse_preset("G | F = 1.0\nG | F = 2.0\n[x.cfg]\nG | F = 3.0\n", "a");
    std::vector<Applied> got = preset_values({a}, fx);
    CHECK(got.size() == 1 && got[0].word == float_word(3.0f) && got[0].line == 4);
    got = preset_values({a}, fy);   // y.cfg: only the plain lines reach it; the later wins
    CHECK(got.size() == 1 && got[0].word == float_word(2.0f));
    const Preset k = parse_preset("[x.cfg]\nG | F = 4.0\n[]\n" + hex8(std::uint32_t(fx.key)) + " = 5.0\n", "k");
    got = preset_values({k}, fx);
    CHECK(got.size() == 1 && got[0].word == float_word(5.0f));
    // across mods: the first in the load order wins, element by element
    const Preset first = parse_preset("G | Arr[1] = 11.0\n", "first");
    const Preset second = parse_preset("G | Arr = [1.0, 2.0, 3.0, 4.0]\n", "second");
    got = preset_values({first, second}, arr);
    CHECK(got.size() == 4);
    CHECK(got[0].word == float_word(1.0f) && got[0].mod == "second");
    CHECK(got[1].word == float_word(11.0f) && got[1].mod == "first");
    got = preset_values({second, first}, arr);
    CHECK(got[1].word == float_word(2.0f) && got[1].mod == "second");
    // clamped to the range, with a note; a value of the wrong kind is a note and nothing written
    std::vector<std::string> notes;
    got = preset_values({parse_preset("G | F = 50.0\nG | I = 2.5\n", "c")}, fx, &notes);
    CHECK(got.size() == 1 && got[0].word == float_word(10.0f) && got[0].clamped);
    CHECK(notes.size() == 1 && notes[0].find("clamped") != std::string::npos);
    notes.clear();
    got = preset_values({parse_preset("G | I = 2.5\n", "c")}, in, &notes);
    CHECK(got.empty() && notes.size() == 1);
    // vectors: (x, y, z) leaves w alone; (x, y, z, w) sets it
    got = preset_values({parse_preset("Cam/ | Offset = (1.0, 2.0, 3.0)\n", "v")}, vec);
    CHECK(got.size() == 3 && got[2].word == float_word(3.0f));
    got = preset_values({parse_preset("Cam/ | Offset = (1.0, 2.0, 3.0, 4.0)\n", "v")}, vec);
    CHECK(got.size() == 4);
    notes.clear();
    got = preset_values({parse_preset("G | Arr = [1.0, 2.0]\n", "short")}, arr, &notes);
    CHECK(got.empty() && notes.size() == 1);
}

TEST(events_text_round_trip) {
    const Event ev{20600, std::int32_t(0x9169203Eu), 2, 0x42F00000u};
    CHECK(event_value(ev) == "0x9169203E[2] 0x42F00000");
    const std::string line = event_line(ev, "G | Arr[2] = 120.0");
    CHECK(line == "tune 20600 0x9169203E[2] 0x42F00000  # G | Arr[2] = 120.0");
    Event back;
    CHECK(parse_event_line(line, back) && back == ev);
    CHECK(parse_event_line("tune 5 0x00000001[0] 0x00000002", back) && back.vblank == 5 && back.key == 1 && back.word == 2);
    CHECK(!parse_event_line("tune x 0x1[0] 0x2", back));
    CHECK(!parse_event_line("tune 5 0x1 0x2", back));
    CHECK(!parse_event_line("rumble 5 1", back));
    Event v;
    CHECK(parse_event_value("0xFFFFFFFF[0] 0x00000000", v) && v.key == -1);
}

TEST(script_parsing) {
    std::vector<std::string> errs;
    const auto s = parse_script("21000:G | F=2.5; 20600:0x12345678[1]=0x10;bad;5:G | F", &errs);
    CHECK(s.size() == 2 && errs.size() == 2);
    CHECK(s[0].vblank == 20600 && s[0].target.by_key && s[0].target.element == 1 && s[0].value == "0x10");
    CHECK(s[1].vblank == 21000 && s[1].target.name == "F" && s[1].value == "2.5");
}

TEST(edits_record_and_replay_exactly) {
    namespace fs = std::filesystem;
    const fs::path dir = fs::path("build") / "test_tune_files";
    fs::create_directories(dir);
    // the recorded session: three variables, a preset, edits on three VBlanks (one variable registered after an edit)
    auto session = [](Toy& t) {
        t.presets = {parse_preset("G | F = 4.0\n", "p")};
        t.registered(reg(0x1000, Type::kFloat, "G", "F", "x.cfg", 0, 0.0f, 10.0f), float_word(1.0f));
        t.registered(reg(0x2000, Type::kInt, "G", "I", "x.cfg", 0, 1, 5), 3u);
        t.registered(reg(0x3000, Type::kFloat, "G", "Arr", "x.cfg", 3), float_word(0.5f));
    };
    Toy rec;
    session(rec);
    CHECK(rec.mem[0x1000] == float_word(4.0f));   // the preset over the game's default
    const Entry& f = rec.r.at(0);
    const Entry& i = rec.r.at(1);
    const Entry& arr = rec.r.at(2);
    const std::vector<Event> live = {{100, f.key, 0, clamp_word(f, float_word(12.0f))},   // the console asks 12: clamped to 10
                                     {100, i.key, 0, step_word(i, 3u, +1)},
                                     {250, arr.key, 2, float_word(9.0f)},
                                     {250, arr.key, 2, float_word(9.0f)}};                 // the same value twice: two events
    // into a pad recording and a replay file, as native_play's on_event does
    b3native::PadRecorder pr;
    CHECK(pr.open((dir / "pads.txt").string()));
    pr.note_gameplay("tuning p");
    b3native::replay::Recorder rr(b3native::replay::Report{});
    for (const Event& ev : live) {
        rec.apply(ev);
        pr.note_tune(event_line(ev, "comment"));
        rr.event(ev.vblank, "tune", event_value(ev));
    }
    // a variable registered after the edits gets them (after the preset)
    rec.registered(reg(0x7000, Type::kFloat, "G", "F", "x.cfg", 0, 0.0f, 10.0f), float_word(1.0f));
    pr.close(300);
    CHECK(rec.mem[0x1000] == float_word(10.0f) && rec.mem[0x7000] == float_word(10.0f));
    CHECK(rec.mem[0x2000] == 4u && rec.mem[0x3008] == float_word(9.0f) && rec.mem[0x3004] == float_word(0.5f));
    CHECK(rec.trace.size() == 4);

    // the pad recording read back: the same events
    b3native::PadReplay back;
    CHECK(back.load((dir / "pads.txt").string(), b3native::PadReport{}));
    CHECK(back.tune_lines().size() == 4 && back.end() == 300);
    Toy play_pad;
    session(play_pad);
    EventQueue q;
    for (const std::string& l : back.tune_lines()) {
        Event ev;
        CHECK(parse_event_line(l, ev));
        q.add(ev);
    }
    for (std::uint64_t v = 0; v <= 300; ++v) {
        for (const Event& ev : q.take(v)) play_pad.apply(ev);
        if (v == 260) play_pad.registered(reg(0x7000, Type::kFloat, "G", "F", "x.cfg", 0, 0.0f, 10.0f), float_word(1.0f));
    }
    CHECK(play_pad.trace == rec.trace);
    CHECK(play_pad.mem == rec.mem);

    // the replay file encoded and decoded: the SETS "tune" entries, every one kept
    b3native::replay::Replay& h = rr.replay();
    h.vblanks = 300;
    h.version = "0.0.0-test";
    const b3native::replay::Bytes bytes = b3native::replay::encode(h);
    std::string why;
    const std::optional<b3native::replay::Replay> decoded = b3native::replay::decode(bytes, &why);
    CHECK(decoded.has_value());
    if (!decoded) return;
    const b3native::replay::Replay& dec = *decoded;
    CHECK(dec.settings.size() == 4);
    b3native::replay::Player player(dec);
    Toy play_file;
    session(play_file);
    for (std::uint64_t v = 0; v <= 300; ++v) {
        for (const b3native::replay::SettingOp* op : player.settings(v)) {
            Event ev;
            CHECK(op->key == "tune" && parse_event_value(op->value, ev));
            ev.vblank = v;
            play_file.apply(ev);
        }
        if (v == 260) play_file.registered(reg(0x7000, Type::kFloat, "G", "F", "x.cfg", 0, 0.0f, 10.0f), float_word(1.0f));
    }
    CHECK(play_file.trace == rec.trace);
    CHECK(play_file.mem == rec.mem);
    fs::remove_all(dir);
}

TEST(saved_preset_reads_back) {
    Registry r;
    r.record(reg(0x1000, Type::kFloat, "G", "F", "x.cfg", 0, 0.0f, 10.0f));
    r.record(reg(0x3000, Type::kFloat, "G", "Arr", "y.cfg", 3));
    Overrides o;
    o.set(r.at(0).key, 0, float_word(7.5f));
    o.set(r.at(1).key, 1, float_word(2.0f));
    const std::string text = overrides_to_preset(o, r, "test");
    const Preset p = parse_preset(text, "saved");
    CHECK(p.errors.empty() && p.lines.size() == 2);
    std::vector<Applied> got = preset_values({p}, r.at(0));
    CHECK(got.size() == 1 && got[0].word == float_word(7.5f));
    got = preset_values({p}, r.at(1));
    CHECK(got.size() == 1 && got[0].element == 1 && got[0].word == float_word(2.0f));
}

TEST(tuning_section_in_a_mod) {
    namespace fs = std::filesystem;
    const fs::path root = fs::path("build") / "test_tune_mods";
    fs::remove_all(root);
    fs::create_directories(root / "with-preset" / "tuning");
    fs::create_directories(root / "plain" / "data");
    fs::create_directories(root / "missing");
    fs::create_directories(root / "escape");
    std::ofstream(root / "with-preset" / "mod.ini") << "[mod]\nid = with-preset\nversion = 1\napi = 1\n[tuning]\npreset = tuning/p.ini\n";
    std::ofstream(root / "with-preset" / "tuning" / "p.ini") << "G | F = 2.0\n";
    std::ofstream(root / "plain" / "mod.ini") << "[mod]\nid = plain\nversion = 1\napi = 1\n";
    std::ofstream(root / "missing" / "mod.ini") << "[mod]\nid = missing\nversion = 1\napi = 1\n[tuning]\npreset = tuning/none.ini\n";
    std::ofstream(root / "escape" / "mod.ini") << "[mod]\nid = escape\nversion = 1\napi = 1\n[tuning]\npreset = ../p.ini\n";
    const std::vector<b3mod::Mod> found = b3mod::discover(root);
    const b3mod::Mod* wp = b3mod::find_mod(found, "with-preset");
    CHECK(wp && wp->usable() && wp->manifest.tuning_preset == "tuning/p.ini");
    bool noted = false;
    for (const std::string& w : wp->warnings) noted = noted || w.find("not used by this port") != std::string::npos;
    CHECK(!noted);
    CHECK(b3mod::find_mod(found, "missing") && !b3mod::find_mod(found, "missing")->usable());
    CHECK(b3mod::find_mod(found, "escape") && !b3mod::find_mod(found, "escape")->usable());
    const b3mod::Overlay o = b3mod::Overlay::build({wp, b3mod::find_mod(found, "plain")});
    CHECK(o.mods().size() == 2);
    CHECK(o.mods()[0].changes_gameplay() && !o.mods()[1].changes_gameplay());
    CHECK(o.mods()[1].hash == b3mod::mod_hash(o.mods()[1].files));   // no preset: the hash as before
    CHECK(o.mods()[0].hash != b3mod::mod_hash(o.mods()[0].files));   // the preset's bytes are in it
    const std::uint64_t before = o.mods()[0].hash;
    std::ofstream(root / "with-preset" / "tuning" / "p.ini") << "G | F = 3.0\n";
    const b3mod::Overlay o2 = b3mod::Overlay::build({wp});
    CHECK(o2.mods()[0].hash != before);
    fs::remove_all(root);
}

TEST_MAIN("test_tune: the tuning console's model (include/b3tune/tune.h)")
