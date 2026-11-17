// Script mods (mod kit Phase 6, lua6; docs/modding/DESIGN.md §10, docs/modding/lua-reference.md): the [script]
// manifest and its files in the mod hash, the sandbox's refusals, the instruction budget and the memory cap stopping a
// script while the game goes on, the seeded math.random, and the `b3` API on a synthetic hook table and synthetic guest
// memory (src/native/script_mods.h on src/native/code_mods.h). The scripts are written by the test under build/. No
// game data.
#include "b3host/call.h"
#include "b3host/native_backend.h"
#include "b3mod/codemod.h"
#include "b3mod/mods.h"
#include "code_mods.h"
#include "hook_table.h"
#include "script_mods.h"
#include "test.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace b3host;
using b3native::CodeMods;
using b3native::GuestArena;
using b3native::HookTable;
using b3native::TableEntry;
namespace fs = std::filesystem;
namespace sa = b3native::script_addr;

#ifndef B3_SCRIPTMODS_DIR
#define B3_SCRIPTMODS_DIR "build/scriptmods"
#endif

namespace {

constexpr gaddr kA = 0x00100000u, kB = 0x00100010u;
int gen_a(int x) { return x * 3; }
int gen_b(int x) { return x + 100; }
// stand-ins at the addresses the script events wrap: OnTakeDown counts at score +0x68, as the game's does
int finish(int car, int state) { return car + state; }
int crash(int car) { return car; }
int takedown(std::uint32_t score, std::uint32_t victim) {
    std::uint32_t n = 0;
    copy_from_guest(&n, score + 0x68, 4);
    if (victim != 0) ++n;   // victim 0 stands for "already taken down": no count
    copy_to_guest(score + 0x68, &n, 4);
    return int(n);
}
std::atomic<bool> g_on{true};
std::atomic<std::uint64_t> g_nested{0};
const NativeOverride kRows[] = {
    {kA, &traced_entry<kA, &native_entry<&gen_a>>, 4, &g_on, &g_nested},
    {kB, &traced_entry<kB, &native_entry<&gen_b>>, 4, &g_on, &g_nested},
    {sa::kSetFinished, &traced_entry<sa::kSetFinished, &native_entry<&finish>>, 4, &g_on, &g_nested},
    {sa::kStartCrashing, &traced_entry<sa::kStartCrashing, &native_entry<&crash>>, 4, &g_on, &g_nested},
    {sa::kOnTakeDown, &traced_entry<sa::kOnTakeDown, &native_entry<&takedown>>, 4, &g_on, &g_nested},
};
const TableEntry kEntries[] = {
    {kA, "leaf_a", "game:matched", 'i'},
    {kB, "leaf_b", "game:matched", 'i'},
    {sa::kSetFinished, "SetFinished", "game:matched", 'i'},
    {sa::kStartCrashing, "StartCrashing", "game:matched", 'i'},
    {sa::kOnTakeDown, "OnTakeDown", "game:matched", 'i'},
};
constexpr std::size_t kN = sizeof kRows / sizeof kRows[0];

// a mod folder build/scriptmods/<id> with scripts/main.lua (and other files)
fs::path write_mod(const std::string& id, const std::string& main, const std::vector<std::pair<std::string, std::string>>& more = {}) {
    const fs::path dir = fs::path(B3_SCRIPTMODS_DIR) / id;
    fs::remove_all(dir);
    fs::create_directories(dir / "scripts");
    std::ofstream(dir / "scripts" / "main.lua", std::ios::binary) << main;
    for (const auto& [rel, text] : more) {
        fs::create_directories((dir / rel).parent_path());
        std::ofstream(dir / rel, std::ios::binary) << text;
    }
    return dir;
}

struct Fixture {   // guest RAM (a NativeBackend), a private hook table, a fresh arena, a loader
    NativeBackend nb;
    BackendScope scope{nb};
    HookTable table;
    GuestArena arena;
    std::vector<std::string> log;
    std::uint64_t vb = 0;
    std::unique_ptr<CodeMods> mods;
    Fixture() {
        nb.set_stack_pointer(0x01F00000u);
        table.attach_rows(kRows, kEntries, kN, true);   // published: b3.call goes through dispatch
        CodeMods::Host h;
        h.port_version = "test";
        h.vblank = [this] { return vb; };
        h.disc_size = [](const std::string& p) -> std::int64_t { return p == "DATA/TEST.BIN" ? 3 : -1; };
        h.disc_read = [](const std::string& p, std::uint64_t off, void* dst, std::uint64_t n) -> std::int64_t {
            if (p != "DATA/TEST.BIN" || off > 3) return -1;
            const std::uint64_t k = std::min<std::uint64_t>(n, 3 - off);
            std::memcpy(dst, "abc" + off, k);
            return std::int64_t(k);
        };
        h.log = [this](const std::string& l) { log.push_back(l); };
        mods = std::make_unique<CodeMods>(table, arena, h);
    }
    ~Fixture() {
        mods.reset();
        set_native_overrides(nullptr, 0);
    }
    b3native::ScriptConfig config(const std::string& id, bool gameplay = true) {
        b3native::ScriptConfig c;
        c.id = id;
        c.version = "1";
        c.dir = fs::absolute(fs::path(B3_SCRIPTMODS_DIR) / id);
        c.main_file = "scripts/main.lua";
        c.gameplay = gameplay;
        return c;
    }
    CodeMods::Candidate candidate(const b3native::ScriptConfig& c) {
        CodeMods::Candidate k;
        k.id = c.id;
        k.version = c.version;
        k.dir = c.dir;
        k.builtin = b3native::make_script_mod(c);
        return k;
    }
    // writes the mod and loads it: loaded or not
    bool load(const std::string& id, const std::string& main, bool gameplay = true,
              const std::vector<std::pair<std::string, std::string>>& more = {}) {
        write_mod(id, main, more);
        return load_config(config(id, gameplay));
    }
    bool load_config(const b3native::ScriptConfig& c) {
        return mods->load({candidate(c)}, false)[0].loaded;
    }
    void vblank() {
        ++vb;
        mods->vblank();
    }
    bool logged(const std::string& part) const {
        for (const std::string& l : log)
            if (l.find(part) != std::string::npos) return true;
        return false;
    }
    std::vector<std::string> lines(const std::string& prefix) const {
        std::vector<std::string> v;
        for (const std::string& l : log)
            if (l.rfind(prefix, 0) == 0) v.push_back(l.substr(prefix.size()));
        return v;
    }
    void dump() const {
        for (const std::string& l : log) std::printf("    | %s\n", l.c_str());
    }
};

int call(HookTable& t, gaddr a, std::uint32_t a0, std::uint32_t a1 = 0) {
    CallArgs c;
    c.gpr[0] = detail::gpr64(a0);
    c.gpr[1] = detail::gpr64(a1);
    c.n_gpr = c.n_fpr = 8;
    c.sp = 0x01F00000u;
    CallResult r;
    t.top(a)(c, r);
    return int(r.v0.w[0]);
}
void put32(std::uint32_t a, std::uint32_t v) { copy_to_guest(a, &v, 4); }
void putf(std::uint32_t a, float v) { copy_to_guest(a, &v, 4); }

}  // namespace

// ---------------------------------------------------------------------------------------------------- the manifest
TEST(script_section_of_the_manifest) {
    auto parse = [](const std::string& t) { return b3mod::parse_manifest(t); };
    const auto ok = parse("[mod]\nid = s\nversion = 1\napi = 3\n[script]\nmain = scripts/main.lua\ngameplay = no\nbudget = 20000\nmemory = 8\n");
    CHECK(ok.errors.empty() && ok.warnings.empty() && ok.m.script.present && ok.m.script.main_file == "scripts/main.lua" && !ok.m.script.gameplay &&
          ok.m.script.budget == 20000 && ok.m.script.memory_mb == 8 && ok.m.script.folder() == "scripts");
    const auto dflt = parse("[mod]\nid = s\nversion = 1\napi = 3\n[script]\nmain = lua/a/b.lua\n");
    CHECK(dflt.errors.empty() && dflt.m.script.gameplay && dflt.m.script.budget == b3mod::ScriptSpec::kDefaultBudget &&
          dflt.m.script.folder() == "lua/a");
    CHECK(dflt.warnings.size() == 1 && dflt.warnings[0].find("read as gameplay = yes") != std::string::npos);
    const auto api2 = parse("[mod]\nid = s\nversion = 1\napi = 2\n[script]\nmain = scripts/main.lua\ngameplay = no\n");
    CHECK(api2.errors.size() == 1 && api2.errors[0].find("[script] needs api = 3") == 0);
    CHECK(!parse("[mod]\nid = s\nversion = 1\napi = 3\n[script]\nmain = main.lua\ngameplay = no\n").errors.empty());       // at the top
    CHECK(!parse("[mod]\nid = s\nversion = 1\napi = 3\n[script]\nmain = ../x/main.lua\ngameplay = no\n").errors.empty());
    CHECK(!parse("[mod]\nid = s\nversion = 1\napi = 3\n[script]\nmain = scripts/main.txt\ngameplay = no\n").errors.empty());
    CHECK(!parse("[mod]\nid = s\nversion = 1\napi = 3\n[script]\ngameplay = no\n").errors.empty());                          // no main
    CHECK(!parse("[mod]\nid = s\nversion = 1\napi = 3\n[script]\nmain = s/m.lua\nbudget = 5\ngameplay = no\n").errors.empty());
    CHECK(!parse("[mod]\nid = s\nversion = 1\napi = 3\n[script]\nmain = s/m.lua\nmemory = 9999\ngameplay = no\n").errors.empty());
    CHECK(!parse("[mod]\nid = s\nversion = 1\napi = 3\n[script]\nmain = s/m.lua\ngameplay = no\n[code]\nlinux = a.so\n").errors.empty());
}

TEST(script_files_are_in_the_mod_hash) {
    const fs::path root = fs::path(B3_SCRIPTMODS_DIR) / "hashroot";
    fs::remove_all(root);
    fs::create_directories(root / "hm" / "scripts" / "lib");
    std::ofstream(root / "hm" / "mod.ini") << "[mod]\nid = hm\nversion = 1\napi = 3\n[script]\nmain = scripts/main.lua\ngameplay = no\n";
    std::ofstream(root / "hm" / "scripts" / "main.lua", std::ios::binary) << "b3.log('a')\n";
    std::ofstream(root / "hm" / "scripts" / "lib" / "util.lua", std::ios::binary) << "return 1\n";
    std::ofstream(root / "hm" / "scripts" / ".hidden", std::ios::binary) << "x";
    auto build = [&] {
        const auto found = b3mod::discover(root);
        return b3mod::Overlay::build({b3mod::find_mod(found, "hm")});
    };
    const b3mod::Overlay a = build();
    CHECK(a.mods().size() == 1 && a.mods()[0].script.present);
    const std::string& lines = a.mods()[0].script_lines;
    CHECK(lines.find("script scripts/main.lua 12 ") != std::string::npos && lines.find("script scripts/lib/util.lua 9 ") != std::string::npos);
    CHECK(lines.find(".hidden") == std::string::npos);
    CHECK(b3mod::describe(a).find("(0 files, script)") != std::string::npos);
    // one byte of a required file changes the set hash
    std::ofstream(root / "hm" / "scripts" / "lib" / "util.lua", std::ios::binary) << "return 2\n";
    const b3mod::Overlay b = build();
    CHECK(b.set_hash() != a.set_hash());
    // a missing main is named
    fs::remove(root / "hm" / "scripts" / "main.lua");
    const b3mod::Overlay c = build();
    CHECK(c.mods()[0].script_lines.find("script scripts/main.lua missing") != std::string::npos && c.set_hash() != b.set_hash());
}

// ---------------------------------------------------------------------------------------------------- the sandbox
TEST(sandbox_refusals) {
    Fixture f;
    const std::string main = R"(
        local function gone(x) return x == nil end
        assert(gone(io) and gone(os) and gone(package) and gone(debug), "a library that must not be there")
        assert(gone(dofile) and gone(loadfile) and gone(string.dump), "a file or bytecode function")
        -- load: text only (a precompiled chunk is refused), a text chunk works
        local f, err = load("\27Lua" .. string.rep("\0", 40))
        assert(f == nil and err:find("binary"), "binary chunk: " .. tostring(err))
        assert(load("return 1 + 1")() == 2)
        -- require: only the script's folder
        assert(require("lib.util").twice(4) == 8 and require("lib.util") == require("lib.util"))
        assert(not pcall(require, "../mod"), "require outside")
        assert(not pcall(require, "/etc/passwd"), "require an absolute path")
        local ok, e = pcall(require, "nothere")
        assert(not ok and e:find("no file scripts/nothere.lua"), e)
        -- files: the mod's folder and the disc through the overlay, read-only; nothing outside
        assert(b3.data.read("data/note.txt") == "note")
        assert(b3.data.read("disc:DATA/TEST.BIN") == "abc" and b3.data.size("disc:DATA/TEST.BIN") == 3)
        assert(b3.data.read("disc:DATA/TEST.BIN", 1, 1) == "b")
        assert(b3.data.read("../hashroot/hm/mod.ini") == nil and b3.data.read("/etc/hostname") == nil)
        local h = b3.data.open("data/note.txt")
        assert(h:read(2) == "no" and h:tell() == 2 and h:read() == "te" and h:read() == nil and h:size() == 4)
        assert(b3.data.open("data/none.txt") == nil)
        -- no clock: math.randomseed() needs a seed
        assert(not pcall(math.randomseed))
        b3.log("sandbox ok")
    )";
    CHECK(f.load("sandbox", main, false, {{"scripts/lib/util.lua", "return { twice = function(x) return 2 * x end }"}, {"data/note.txt", "note"}}));
    CHECK(f.logged("mod sandbox: sandbox ok"));
    if (!f.logged("sandbox ok")) f.dump();
}

TEST(main_chunk_errors_leave_the_mod_out) {
    Fixture f;
    CHECK(!f.load("syntax", "b3.log('x'"));
    CHECK(f.logged("mod syntax: error: the script did not load: an error") && f.logged("main.lua:1:"));
    CHECK(!f.load("runtime", "local t = nil\nreturn t.x\n"));
    CHECK(f.logged("main.lua:2: attempt to index a nil value"));
    CHECK(!f.load("forever", "while true do end"));
    CHECK(f.logged("mod forever: error: the script did not load: its main chunk ran past 50000000 Lua instructions"));
    CHECK(!f.mods->any());
}

// ---------------------------------------------------------------------------------------------------- budget, memory
TEST(budget_stops_a_runaway_script_and_the_game_goes_on) {
    Fixture f;
    const std::string main = R"(
        b3.hook.wrap(0x00100010, function(call) call:next() call:set_result(call:result() + 1) end)
        local n = 0
        b3.on_vblank(function(vb)
            n = n + 1
            b3.log("vblank", vb)
            if vb == 3 then
                while true do pcall(function() while true do end end) end   -- a pcall does not keep it running
            end
        end)
    )";
    CHECK(f.load("runaway", main));
    CHECK(call(f.table, kB, 3) == 104);   // the wrap: the game's 103, + 1
    f.vblank();
    f.vblank();
    f.vblank();   // the loop: stopped
    CHECK(f.logged("mod runaway: error: the script was stopped at VBlank 3: it ran past 1000000 Lua instructions in one VBlank"));
    CHECK(f.logged("the game goes on without it"));
    f.vblank();
    f.vblank();
    CHECK(f.lines("mod runaway: vblank ").size() == 3);   // no callback after the stop
    CHECK(call(f.table, kB, 3) == 103);                  // and its hook passes the call to the game
    // a smaller budget from the manifest's figure
    Fixture g;
    write_mod("small", "b3.on_vblank(function() for i = 1, 30000 do end end)");
    auto c = g.config("small");
    c.budget = 20000;
    CHECK(g.load_config(c));
    g.vblank();
    CHECK(g.logged("it ran past 20000 Lua instructions in one VBlank"));
}

TEST(memory_cap_stops_a_script) {
    Fixture f;
    write_mod("hungry", "local t = {}\nb3.on_vblank(function() for i = 1, 1e7 do t[i] = ('x'):rep(64) .. i end end)\n");
    auto c = f.config("hungry");
    c.memory = 1u << 20;   // 1 MB
    c.budget = 50000000;
    CHECK(f.load_config(c));
    f.vblank();
    CHECK(f.logged("mod hungry: error: the script was stopped at VBlank 1: it used more than 1 MB (its memory cap)"));
    if (!f.logged("memory cap")) f.dump();
    f.vblank();   // nothing more runs (and the state was closed)
    CHECK(f.lines("mod hungry: error: the script was stopped").size() == 1);
}

// ---------------------------------------------------------------------------------------------------- determinism
TEST(math_random_is_seeded_from_the_mod_id) {
    const std::string main = "local t = {}\nfor i = 1, 6 do t[#t + 1] = math.random(1, 1000000) end\nb3.log(table.concat(t, ' '))\n"
                             "local k = {}\nfor _, w in ipairs({'lap', 'crash', 'race', 'speed', 'zeta', 'alpha'}) do k[w] = true end\n"
                             "local o = {}\nfor w in pairs(k) do o[#o + 1] = w end\nb3.log('order ' .. table.concat(o, ','))\n";
    std::vector<std::string> runs;
    for (const char* id : {"rnd-a", "rnd-a", "rnd-b"}) {
        Fixture f;
        CHECK(f.load(id, main));
        const auto l = f.lines(std::string("mod ") + id + ": ");
        CHECK(l.size() == 2);
        if (l.size() == 2) runs.push_back(l[0] + " | " + l[1]);
    }
    CHECK(runs.size() == 3 && runs[0] == runs[1] && runs[0] != runs[2]);
    // the string-key order is the same for both ids (string hashing has a fixed seed, script_lua.cpp)
    CHECK(runs.size() == 3 && runs[0].substr(runs[0].find(" | ")) == runs[2].substr(runs[2].find(" | ")));
    // a script seeding itself gets Lua's sequence for that seed
    Fixture f;
    CHECK(f.load("rnd-c", "math.randomseed(42)\nlocal a = math.random(1000)\nmath.randomseed(42)\nb3.log(a == math.random(1000))\n"));
    CHECK(f.logged("mod rnd-c: true"));
}

// ---------------------------------------------------------------------------------------------------- the API
TEST(api_bindings_on_synthetic_memory) {
    Fixture f;
    const std::string main = R"(
        b3.settings.add{ key = "show", label = "Show it", default = true }
        b3.settings.add{ key = "mode", label = "Mode", kind = "choice", values = { "Slow", "Fast" }, default = "Fast" }
        b3.hook.replace(0x00100000, function(call) call:set_result(call:arg(1) * 10) end)
        local tex = b3.draw.texture(2, 1, string.rep("\255", 8))
        b3.on_vblank(function(vb)
            if vb == 1 then
                local m = b3.mem
                assert(m.write("u32", 0x00600000, 0x89ABCDEF) and m.read("u32", 0x00600000) == 0x89ABCDEF)
                assert(m.read("s32", 0x00600000) == 0x89ABCDEF - 0x100000000 and m.read("u8", 0x00600000) == 0xEF)
                assert(m.read("s8", 0x00600000) == -17 and m.read("u16", 0x00600002) == 0x89AB and m.read("s16", 0x00600002) == 0x89AB - 0x10000)
                assert(m.write("f32", 0x00600010, 1.5) and m.read("f32", 0x00600010) == 1.5)
                assert(m.write("s8", 0x00600020, -1) and m.read("u8", 0x00600020) == 255)
                assert(not pcall(m.write, "u8", 0x00600020, 256), "a value that does not fit")
                assert(m.write_bytes(0x00600030, "hey") and m.read_bytes(0x00600030, 3) == "hey")
                assert(m.read("u32", 0x40000000) == nil and m.read_bytes(0x01FFFFFE, 4) == nil)   -- outside RAM
                assert(m.write("u32", 0x00120000, 1) == false)                                       -- the program's code
                assert(m.read("u32", 0x70000000) ~= nil)                                             -- the scratchpad
                -- calls through the hook table: the replace (x 10) and the game's function
                local v0 = b3.call(0x00100000, 7)
                assert(v0 == 70, v0)
                assert(b3.call(0x00100010, 1) == 101 and b3.has_function(0x00100010) and not b3.has_function(0x00100040))
                assert(not pcall(b3.call, 0x00100040), "no function there")
                assert(not pcall(coroutine.wrap(function() return b3.call(0x00100010, 1) end)), "a call from a coroutine")
                -- settings
                assert(b3.settings.get("show") == true and b3.settings.get("mode") == "Fast" and b3.settings.index("mode") == 1)
                assert(b3.settings.get("none") == nil)
                assert(not pcall(b3.settings.add, { key = "late" }), "a row after the main chunk")
                assert(not pcall(b3.hook.wrap, 0x00100010, print), "a hook after the main chunk")
                b3.log("api ok", b3.vblank(), b3.id, b3.version, b3.gameplay, b3.port_version)
            end
        end)
        b3.on_frame(function()
            b3.draw.quad(10, 20, 30, 40, b3.rgba(255, 0, 0), tex)
            b3.draw.text(5, 6, 12, b3.rgba(0, 255, 0, 128), "SPEED 1")
        end)
    )";
    CHECK(f.load("api", main));
    CHECK(f.logged("native_play: mods: api: script loaded (1 replaced, 0 wrapped, 2 settings"));
    CHECK(call(f.table, kA, 4) == 40);
    f.vblank();
    CHECK(f.logged("mod api: api ok 1 api 1 true test"));
    if (!f.logged("api ok")) f.dump();
    f.mods->frame();
    const std::vector<std::uint8_t> rec = f.mods->overlay_record();
    b3mod::OverlayList l;
    CHECK(!rec.empty() && b3mod::overlay_parse(rec.data(), rec.size(), l) && l.items.size() == 2);
    if (l.items.size() == 2) {
        CHECK(l.items[0].kind == 1 && l.items[0].x == 10 && l.items[0].h == 40 && l.items[0].rgba == 0xFF0000FFu && l.items[0].texture != 0);
        CHECK(l.items[1].kind == 2 && l.items[1].text == "SPEED 1" && l.items[1].rgba == 0x8000FF00u && l.items[1].h == 12);
    }
    CHECK(f.mods->settings().rows().size() == 2);
}

TEST(gameplay_no_refuses_writes) {
    Fixture f;
    CHECK(!f.load("ro-replace", "b3.hook.replace(0x00100000, function() end)", false));
    CHECK(f.logged("b3.hook.replace: refused: this mod says gameplay = no"));
    const std::string main = R"(
        b3.hook.wrap(0x00100010, function(call)
            local v0 = call:next()
            if not pcall(call.set_result, call, 0) then b3.log("set_result refused") end
        end)
        b3.on_vblank(function(vb)
            assert(b3.mem.read("u32", 0x00200000) ~= nil)
            if not pcall(b3.call, 0x00100010, 1) then b3.log("call refused") end
            b3.mem.write("u8", 0x00200000, 1)   -- refused: an error, the script stops
        end)
    )";
    CHECK(f.load("ro", main, false));
    CHECK(call(f.table, kB, 2) == 102);   // a read-only wrap cannot change the result
    CHECK(f.logged("mod ro: set_result refused"));
    put32(0x00200000, 7);
    f.vblank();
    CHECK(f.logged("mod ro: call refused"));
    CHECK(f.logged("b3.mem.write: refused: this mod says gameplay = no"));
    std::uint32_t v = 0;
    copy_from_guest(&v, 0x00200000, 4);
    CHECK(v == 7);
}

TEST(errors_stop_the_script_and_hooks_pass_through) {
    Fixture f;
    CHECK(f.load("oops", "b3.hook.wrap(0x00100010, function(call) error('boom') end)\nb3.on_vblank(function() b3.log('tick') end)\n"));
    CHECK(call(f.table, kB, 1) == 101);   // the hook failed: the game's call ran, the script stopped
    CHECK(f.logged("mod oops: error: the script was stopped at VBlank 0: an error") && f.logged("main.lua:1: boom"));
    f.vblank();
    CHECK(f.lines("mod oops: tick").empty());
    CHECK(call(f.table, kB, 1) == 101);
}

TEST(views_and_events) {
    Fixture f;
    const std::string main = R"(
        b3.on("race_start", function(e) b3.log("race_start", e.vblank) end)
        b3.on("lap", function(e) b3.log("lap", e.lap, e.time) end)
        b3.on("finish", function(e) b3.log("finish", e.player, e.state) end)
        b3.on("crash", function(e) b3.log("crash", e.player) end)
        b3.on("takedown", function(e) b3.log("takedown", e.player, e.victim_player, e.count) end)
        assert(not pcall(b3.on, "explode", print))
        b3.on_vblank(function(vb)
            local r, p, s = b3.view.race(), b3.view.player(), b3.view.score()
            if vb == 1 then assert(not r.active and p == nil and s == nil) end
            if vb == 4 then
                assert(r.active and r.cars == 2 and r.timer == 12.5 and not r.split_screen)
                assert(p.x == 1 and p.y == 2 and p.z == 3 and p.speed == 20 and p.speed_shown == 72, p.speed_shown)
                assert(s.state == 2 and s.time == 61.25 and s.laps_done == 1 and s.lap_times[1] == 30.5 and s.takedowns == 0)
                b3.log("views ok")
            end
        end)
    )";
    CHECK(f.load("ev", main));
    CHECK(f.logged("native_play: mods: ev: script loaded (0 replaced, 3 wrapped"));
    f.vblank();   // no race
    // a race: two cars, the player's first; its score at state 1 (start), then 2 (racing)
    put32(sa::kRaceCarCount, 2);
    put32(sa::kRaceCarList, sa::kPlayerCar);
    put32(sa::kPlayerScore + 0x27C, 1);
    putf(sa::kTimer, 12.5f);
    f.vblank();
    put32(sa::kPlayerScore + 0x27C, 2);
    f.vblank();   // race_start
    putf(sa::kPlayerCar + 0x30, 1);
    putf(sa::kPlayerCar + 0x34, 2);
    putf(sa::kPlayerCar + 0x38, 3);
    putf(sa::kSpeedVector + 0xC, 20);
    put32(sa::kSpeedUnits, 1);
    putf(sa::kSpeedFactorLow, 3.6f);
    putf(sa::kPlayerScore + 0xC, 61.25f);
    put32(sa::kPlayerScore + 0x18, 1);
    putf(sa::kPlayerScore + 0x1C, 30.5f);
    f.vblank();   // lap 1; the views
    CHECK(f.logged("mod ev: race_start 3"));
    CHECK(f.logged("mod ev: lap 1 30.5"));
    CHECK(f.logged("mod ev: views ok"));
    if (!f.logged("views ok")) f.dump();
    // the wrapped functions: the player's finish, a crash of another car, two takedown calls (one counted)
    CHECK(call(f.table, sa::kSetFinished, sa::kPlayerCar, 1) == int(sa::kPlayerCar + 1));
    CHECK(call(f.table, sa::kStartCrashing, 0x00700000u) == 0x00700000);
    put32(0x00300000u + 0xC8, sa::kPlayerCar);   // a score whose car is the player's
    call(f.table, sa::kOnTakeDown, 0x00300000u, 0x00700000u);
    call(f.table, sa::kOnTakeDown, 0x00300000u, 0);
    CHECK(f.logged("mod ev: finish true 1") && f.logged("mod ev: crash false"));
    CHECK(f.lines("mod ev: takedown true false 1").size() == 1 && f.lines("mod ev: takedown").size() == 1);
}

TEST(hot_reload_with_developer_mods) {
    Fixture f;
    write_mod("hot", "local n = 0\nb3.on_load(function(again) b3.log('load', again) end)\nb3.on_vblank(function() n = n + 1 b3.log('v1', n) end)\n");
    auto c = f.config("hot");
    c.developer = true;
    c.reload_every = 1;
    CHECK(f.load_config(c));
    CHECK(f.logged("mod hot: load false"));
    f.vblank();
    std::ofstream(fs::path(B3_SCRIPTMODS_DIR) / "hot" / "scripts" / "main.lua", std::ios::binary)
        << "b3.on_load(function(again) b3.log('load', again) end)\nb3.on_vblank(function() b3.log('v2') end)\n-- a longer file\n";
    f.vblank();
    CHECK(f.logged("mod hot: its script changed: loaded again at VBlank 2"));
    CHECK(f.logged("mod hot: load true") && f.logged("mod hot: v2"));
    // a broken edit stops it; the next good one brings it back
    std::ofstream(fs::path(B3_SCRIPTMODS_DIR) / "hot" / "scripts" / "main.lua", std::ios::binary) << "b3.on_vblank(\n";
    f.vblank();
    CHECK(f.logged("mod hot: error: the script did not load: an error"));
    std::ofstream(fs::path(B3_SCRIPTMODS_DIR) / "hot" / "scripts" / "main.lua", std::ios::binary) << "b3.on_vblank(function() b3.log('v3') end)\n";
    f.vblank();
    CHECK(f.logged("mod hot: v3"));
    // without Developer mods a change is not looked for
    Fixture g;
    write_mod("cold", "b3.on_vblank(function() b3.log('c1') end)\n");
    auto d = g.config("cold");
    d.reload_every = 1;
    CHECK(g.load_config(d));
    std::ofstream(fs::path(B3_SCRIPTMODS_DIR) / "cold" / "scripts" / "main.lua", std::ios::binary) << "b3.on_vblank(function() b3.log('c2') end)\n";
    g.vblank();
    g.vblank();
    CHECK(g.logged("mod cold: c1") && !g.logged("c2") && !g.logged("loaded again"));
}

TEST_MAIN("script mods (lua6: src/native/script_mods.h)")
