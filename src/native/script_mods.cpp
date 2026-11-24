// Script mods: the Lua runtime behind [script] (script_mods.h; docs/modding/lua-reference.md).
#include "script_mods.h"

#include "../../sdk/include/b3mod_api.h"
#include "b3host/host.h"

// Lua is compiled as C++ (script_lua.cpp): its headers without lua.hpp's extern "C"
#include "../third_party/lua-5.4.9/src/lua.h"
#include "../third_party/lua-5.4.9/src/lauxlib.h"
#include "../third_party/lua-5.4.9/src/lualib.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <cstring>
#include <exception>
#include <map>
#include <system_error>
#include <vector>

namespace b3native {

namespace {
class ScriptMod;
thread_local ScriptMod* g_running = nullptr;   // the script whose state runs now on this thread (its outermost entry)
}  // namespace
}  // namespace b3native

namespace b3native {

namespace {

namespace fs = std::filesystem;
namespace sa = script_addr;

constexpr int kStep = 1000;   // the count hook's period (instructions): the budget is looked at every kStep

// the game events a script can listen to (b3.on): the first three are wraps on the game's functions, the other two are
// read from the player's score at each VBlank
enum Event { kFinish, kCrash, kTakedown, kRaceStart, kLap, kEvents };
const char* const kEventNames[kEvents] = {"finish", "crash", "takedown", "race_start", "lap"};
const std::uint32_t kEventAddr[3] = {sa::kSetFinished, sa::kStartCrashing, sa::kOnTakeDown};

std::string hex(std::uint32_t a) {
    char b[16];
    std::snprintf(b, sizeof b, "0x%08X", a);
    return b;
}
std::string event_list(int e) { return std::string("event:") + kEventNames[e]; }
std::string folder_of(const std::string& file) {
    b3mod::ScriptSpec sp;
    sp.main_file = file;
    return sp.folder();
}

// registry keys (their addresses: unique in the process)
char kListsKey, kLoadedKey, kHooksKey;

// one running hook as the script sees it (b3.hook: the `call` object)
struct CallState {
    B3ModHookCtx* ctx;
    B3ModRegs regs;
    B3ModResult result;
    bool next_called = false;
    bool replace = false;
};

int message_handler(lua_State* L) {
    const char* msg = lua_tostring(L, 1);
    if (!msg) msg = luaL_typename(L, 1);
    luaL_traceback(L, L, msg, 1);
    return 1;
}

// a list of functions in the registry (on_vblank, ...)
void push_list(lua_State* L, const char* name) {
    lua_rawgetp(L, LUA_REGISTRYINDEX, &kListsKey);
    if (lua_getfield(L, -1, name) != LUA_TTABLE) {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushvalue(L, -1);
        lua_setfield(L, -3, name);
    }
    lua_remove(L, -2);
}
void append(lua_State* L, const char* list, int fn) {
    push_list(L, list);
    lua_pushvalue(L, fn);
    lua_rawseti(L, -2, static_cast<lua_Integer>(lua_rawlen(L, -2)) + 1);
    lua_pop(L, 1);
}
void set_num(lua_State* L, const char* k, lua_Number v) {
    lua_pushnumber(L, v);
    lua_setfield(L, -2, k);
}
void set_int(lua_State* L, const char* k, lua_Integer v) {
    lua_pushinteger(L, v);
    lua_setfield(L, -2, k);
}
void set_bool(lua_State* L, const char* k, bool v) {
    lua_pushboolean(L, v);
    lua_setfield(L, -2, k);
}

class ScriptMod final : public CodeMods::Builtin {
public:
    explicit ScriptMod(ScriptConfig c) : cfg_(std::move(c)) {}
    ~ScriptMod() override { close(); }
    const char* kind() const override { return "script"; }
    int main(const B3ModApi* api, B3ModSelf* self) override;

    // ---- state ----
    ScriptConfig cfg_;
    const B3ModApi* api_ = nullptr;
    B3ModSelf* self_ = nullptr;
    lua_State* L_ = nullptr;
    std::size_t mem_ = 0, cap_ = 0;
    bool mem_hit_ = false;
    std::uint64_t used_ = 0, limit_ = 0;
    bool over_ = false;
    // modsec: the time limit, wall clock, of the script's own time (from each outermost entry into the state to its
    // return) per VBlank (the main chunk and on_load: kScriptLoadTimeFactor times it): the budget counts Lua
    // instructions only, and a C library call (a pattern that backtracks, a huge string) runs none. Looked at in the
    // count hook, in the allocator and at each step of the pattern matcher (script_lua.cpp); past it the script is
    // stopped as past its budget. A script within its budget never comes near it, so runs stay deterministic.
    std::int64_t time_used_ = 0;   // ns
    std::chrono::steady_clock::time_point t0_{};
    bool timed_out_ = false;
    unsigned ticks_ = 0;
    std::int64_t time_limit() const {
        const std::int64_t per = std::int64_t(cfg_.time_limit_ms) * 1000000;
        return limit_ > cfg_.budget ? per * std::int64_t(kScriptLoadTimeFactor) : per;
    }
    bool past_time() {   // (only while it runs: g_running == this)
        if (timed_out_) return true;
        if (time_used_ + (std::chrono::steady_clock::now() - t0_).count() > time_limit()) timed_out_ = true;
        return timed_out_;
    }
    // review: the clock runs only while this script's own code runs. Every entry into its state makes it the running
    // script (the one before it paused); its calls out into the game's code (b3.call, call:next) pause it, so the
    // game's time (a track load, a VBlank wait while the window is unfocused) and another script's hooks the game
    // reaches are not charged to it.
    void clock_in() {
        t0_ = std::chrono::steady_clock::now();
        g_running = this;
    }
    void clock_out() {
        time_used_ += (std::chrono::steady_clock::now() - t0_).count();
        g_running = nullptr;
    }
    bool in_main_ = false;     // inside the loader's call (the first load: registrations go to the API)
    bool reloading_ = false;   // a hot reload's main chunk (registrations matched against the first load's)
    bool dead_ = false;        // stopped: nothing more runs in the state
    int depth_ = 0;            // protected calls into the state now running (a hook inside b3.call: 2)
    // modsec: the host's fatal error (b3host::HostError) met in the game's code the script called (b3.call,
    // call:next). The script must not see it (a pcall would make it a probe, and the game's function stopped halfway):
    // the binding keeps it here, stops the script (every later instruction raises), and protect() throws it on to the
    // game once the state has unwound, as if no script had been there
    std::exception_ptr fatal_;
    std::string fatal_what_;
    std::string warn_;
    std::vector<std::string> wants_;   // the event lists with a handler (no Lua call to find out)

    struct HookRec {
        ScriptMod* m;
        std::uint32_t addr;
        bool replace;
        int slot;           // its function: the registry's hooks table [slot]
        bool bound;         // the current state gave it a function
    };
    std::vector<std::unique_ptr<HookRec>> hooks_;
    struct EventRec {
        ScriptMod* m;
        int event;
    };
    std::vector<std::unique_ptr<EventRec>> event_recs_;
    bool installed_[3] = {};
    struct Row {
        std::string key;
        int kind;                          // B3MOD_SETTING_*
        std::vector<std::string> values;   // a choice's names
        unsigned count;
    };
    std::vector<Row> rows_;
    int last_state_ = -1, last_laps_ = -1;   // the polled events
    std::uint64_t sig_ = 0, next_look_ = 0;  // hot reload
    // an unsafe script's files as read and hashed at load (path inside the mod -> bytes): the main chunk and require()
    // read only these, so what runs is what was checked
    bool snapped_ = false;
    std::map<std::string, std::string> snap_;
    bool snapshot(bool check);
    const std::string* snapped(const std::string& rel) const {
        const auto it = snap_.find(rel);
        return it == snap_.end() ? nullptr : &it->second;
    }

    // ---- helpers ----
    void log(int level, const std::string& s) { api_->log(self_, level, s.c_str()); }
    std::uint64_t vblank() const { return api_->vblank(); }
    bool rd(std::uint32_t a, void* d, std::uint32_t n) const { return api_->read(self_, a, d, n) != 0; }
    std::uint32_t u32(std::uint32_t a) const {
        std::uint32_t v = 0;
        return rd(a, &v, 4) ? v : 0;
    }
    float f32(std::uint32_t a) const {
        float v = 0;
        return rd(a, &v, 4) ? v : 0.0f;
    }
    bool in_race() const {
        const std::uint32_t n = u32(sa::kRaceCarCount);
        return n >= 1 && n <= 8 && u32(sa::kRaceCarList) == sa::kPlayerCar;   // (8: MORE RIVALS' races)
    }
    bool wants(int e) const { return std::find(wants_.begin(), wants_.end(), event_list(e)) != wants_.end(); }

    bool open_state();
    void close() {
        if (L_) lua_close(L_);
        L_ = nullptr;
        mem_ = 0;
    }
    void close_if_dead() {
        if (dead_ && L_ && depth_ == 0) close();
    }
    // Runs f(L) as one protected call into the state: every push and every handler of one entry from the game. An
    // error, the budget or the memory cap stops the script (false); handlers inside f are called with lua_call.
    template <class F>
    bool protect(F&& f);
    void stop(int rc, const std::string& msg);
    bool run_main(const std::string& text);
    // every function of a registry list, each with the values push(L) pushes
    template <class P>
    bool run_list(const std::string& list, P&& push);
    std::uint64_t signature() const;
    void reload();

    // the loader's callbacks
    void on_vblank();
    void on_frame();
    void on_shutdown();
};

ScriptMod& self_of(lua_State* L) { return **static_cast<ScriptMod**>(lua_getextraspace(L)); }

// ================================================================================================ allocator, budget
void* lua_alloc(void* ud, void* ptr, std::size_t osize, std::size_t nsize) {
    ScriptMod& m = *static_cast<ScriptMod*>(ud);
    const std::size_t old = ptr ? osize : 0;
    if (nsize == 0) {
        if (ptr) {
            m.mem_ -= old;
            std::free(ptr);
        }
        return nullptr;
    }
    if (nsize > old && m.mem_ + (nsize - old) > m.cap_) {   // the memory cap (a shrink never fails)
        m.mem_hit_ = true;
        return nullptr;
    }
    if (nsize > old && g_running == &m && (m.timed_out_ || ((++m.ticks_ & 63u) == 0 && m.past_time()))) return nullptr;   // its time
    void* p = std::realloc(ptr, nsize);
    if (!p) return nullptr;
    m.mem_ = m.mem_ - old + nsize;
    return p;
}

// the instruction budget, every kStep instructions; past it (or once stopped) an error at every instruction, so no
// pcall in the script can keep it running
void count_hook(lua_State* L, lua_Debug*) {
    ScriptMod& m = self_of(L);
    if (!m.over_) {
        m.used_ += kStep;
        if (m.used_ <= m.limit_ && !m.dead_ && !(g_running == &m && m.past_time())) return;
        m.over_ = !m.dead_ || m.over_;
    }
    lua_sethook(L, &count_hook, LUA_MASKCOUNT, 1);
    luaL_error(L, "the script was stopped (its instruction budget, or an error before)");
}

void warn_fn(void* ud, const char* msg, int tocont) {
    ScriptMod& m = *static_cast<ScriptMod*>(ud);
    if (m.warn_.empty() && msg[0] == '@') return;   // control messages ("@on", "@off"): warnings always go to the log
    m.warn_ += msg;
    if (!tocont) {
        m.log(B3MOD_LOG_WARN, m.warn_);
        m.warn_.clear();
    }
}

// the game's code called from a binding (b3.call, call:next): a HostError there is kept for protect() and the script
// stopped; nothing the script runs after it (a pcall's handler, the next instruction) gets past the count hook
// a HostError met in a binding: kept (the first one), the script stopped; raised in the state as an error nothing in the
// script gets past (the count hook raises again at every instruction)
[[noreturn]] void host_fatal(lua_State* L, ScriptMod& m, const b3host::HostError& e) {
    if (!m.fatal_) {
        m.fatal_ = std::make_exception_ptr(e);
        m.fatal_what_ = e.what();
    }
    m.dead_ = true;
    lua_sethook(L, &count_hook, LUA_MASKCOUNT, 1);
    luaL_error(L, "the game's code stopped with a fatal error");
    std::abort();   // (luaL_error does not return)
}
template <class G>
void game_code(lua_State* L, ScriptMod& m, G&& g) {
    bool hit = false;
    std::string what;
    struct Pause {   // the game's own time is not the script's
        ScriptMod& m;
        explicit Pause(ScriptMod& s) : m(s) { m.clock_out(); }
        ~Pause() { m.clock_in(); }
    };
    try {
        Pause p(m);
        g();
    } catch (const b3host::HostError& e) {
        if (!m.fatal_) {
            m.fatal_ = std::current_exception();
            m.fatal_what_ = e.what();
        }
        hit = true;
    }
    if (!hit) return;
    m.dead_ = true;
    lua_sethook(L, &count_hook, LUA_MASKCOUNT, 1);
    luaL_error(L, "the game's code stopped with a fatal error");
}
// every C function a script can call: a HostError inside it (a guest read, a view, a draw ...) is the game's, never a
// Lua error a pcall could catch. Lua's own errors (thrown through C++ here) pass untouched.
template <lua_CFunction F>
int guarded(lua_State* L) {
    try {
        return F(L);
    } catch (const b3host::HostError& e) {
        host_fatal(L, self_of(L), e);
    }
}

template <class F>
bool ScriptMod::protect(F&& f) {
    lua_State* L = L_;
    if (!L || dead_ || !lua_checkstack(L, 16)) return false;
    struct Box {
        std::remove_reference_t<F>* f;
    } box{&f};
    const int top = lua_gettop(L);
    lua_pushcfunction(L, &message_handler);
    lua_pushcfunction(L, [](lua_State* S) -> int {
        auto* b = static_cast<Box*>(lua_touserdata(S, 1));
        lua_settop(S, 0);
        (*b->f)(S);
        return 0;
    });
    lua_pushlightuserdata(L, &box);
    struct Clock {   // this script's time from its entry to its return (also through a rethrown fatal); the one it interrupted paused
        ScriptMod& m;
        ScriptMod* prev;
        explicit Clock(ScriptMod& s) : m(s), prev(g_running) {
            if (prev) prev->clock_out();
            m.clock_in();
        }
        ~Clock() {
            m.clock_out();
            if (prev) prev->clock_in();
        }
    } clock(*this);
    ++depth_;
    const int rc = lua_pcall(L, 1, 0, top + 1);
    --depth_;
    if (rc != LUA_OK) {
        std::size_t n = 0;
        const char* s = lua_tolstring(L, -1, &n);
        const std::string msg = s ? std::string(s, n) : std::string("(an error that is not a string)");
        lua_settop(L, top);
        if (fatal_) {   // the game's own fatal error, through the script: on to the game (the outermost entry closes the state)
            const std::exception_ptr e = fatal_;
            if (depth_ == 0) {
                dead_ = false;   // (so stop() logs it)
                stop(rc, "");
                fatal_ = nullptr;
                close();
            }
            std::rethrow_exception(e);
        }
        stop(rc, msg);
        return false;
    }
    lua_settop(L, top);
    return true;
}

template <class P>
bool ScriptMod::run_list(const std::string& name, P&& push) {
    return protect([&](lua_State* L) {
        push_list(L, name.c_str());
        const int list = lua_gettop(L);
        push(L);
        const int nargs = lua_gettop(L) - list;
        const lua_Integer n = lua_Integer(lua_rawlen(L, list));
        for (lua_Integer i = 1; i <= n; ++i) {
            lua_rawgeti(L, list, i);
            for (int k = 1; k <= nargs; ++k) lua_pushvalue(L, list + k);
            lua_call(L, nargs, 0);
        }
    });
}

// ================================================================================================ argument helpers
std::uint32_t check_addr(lua_State* L, int i) {
    const lua_Integer a = luaL_checkinteger(L, i);
    if (a < 0 || a > 0xFFFFFFFFll) luaL_argerror(L, i, "not a 32-bit guest address");
    return static_cast<std::uint32_t>(a);
}
void need_gameplay(lua_State* L, const char* what) {
    if (!self_of(L).cfg_.gameplay) luaL_error(L, "%s: refused: this mod says gameplay = no (it may only read and draw)", what);
}
// modsec: writing the game's memory and calling its functions reach the game's own code with the script's values
void need_unsafe(lua_State* L, const char* what) {
    need_gameplay(L, what);
    if (!self_of(L).cfg_.unsafe)
        luaL_error(L, "%s: refused: only a script whose mod.ini says unsafe = yes in [script] (the player is then asked to allow it, as for a code mod)", what);
}
// game code runs only from the state's main thread: a hook it reaches calls into that thread
void need_main_thread(lua_State* L, const char* what) {
    const bool main = lua_pushthread(L) == 1;
    lua_pop(L, 1);
    if (!main) luaL_error(L, "%s: not inside a coroutine (the game's code is called from the script's main thread only)", what);
}
void need_main_chunk(lua_State* L, const char* what) {
    ScriptMod& m = self_of(L);
    if (!m.in_main_ && !m.reloading_) luaL_error(L, "%s: only in the script's main chunk (it is fixed when the game starts)", what);
}

// ================================================================================================ b3: info, log
std::string join_args(lua_State* L) {
    std::string s;
    const int n = lua_gettop(L);
    for (int i = 1; i <= n; ++i) {
        std::size_t len = 0;
        const char* p = luaL_tolstring(L, i, &len);
        if (i > 1) s += ' ';
        s.append(p, len);
        lua_pop(L, 1);
    }
    return s;
}
int l_log(lua_State* L) {
    self_of(L).log(B3MOD_LOG_INFO, join_args(L));
    return 0;
}
int l_warn(lua_State* L) {
    self_of(L).log(B3MOD_LOG_WARN, join_args(L));
    return 0;
}
int l_vblank(lua_State* L) {
    lua_pushinteger(L, static_cast<lua_Integer>(self_of(L).vblank()));
    return 1;
}
int l_has_function(lua_State* L) {
    ScriptMod& m = self_of(L);
    lua_pushboolean(L, m.api_->has_function(m.self_, check_addr(L, 1)) != 0);
    return 1;
}
int l_rgba(lua_State* L) {
    auto c = [&](int i, lua_Number def) {
        const lua_Number v = lua_isnoneornil(L, i) ? def : luaL_checknumber(L, i);
        return static_cast<std::uint32_t>(std::clamp<lua_Number>(std::floor(v + 0.5), 0, 255));
    };
    lua_pushinteger(L, static_cast<lua_Integer>(B3MOD_RGBA(c(1, 0), c(2, 0), c(3, 0), c(4, 255))));
    return 1;
}

// ================================================================================================ b3: events
int on_list(lua_State* L, const char* list) {
    luaL_checktype(L, 1, LUA_TFUNCTION);
    append(L, list, 1);
    return 0;
}
int l_on_vblank(lua_State* L) { return on_list(L, "vblank"); }
int l_on_frame(lua_State* L) { return on_list(L, "frame"); }
int l_on_shutdown(lua_State* L) { return on_list(L, "shutdown"); }
int l_on_load(lua_State* L) { return on_list(L, "load"); }

void event_trampoline(B3ModHookCtx* ctx, const B3ModRegs* regs, B3ModResult* result, void* user);

int l_on(lua_State* L) {
    ScriptMod& m = self_of(L);
    const char* name = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    int e = -1;
    for (int k = 0; k < kEvents; ++k)
        if (std::strcmp(name, kEventNames[k]) == 0) e = k;
    if (e < 0) return luaL_argerror(L, 1, "not a game event (race_start, lap, crash, takedown, finish)");
    if (e < 3 && !m.installed_[e]) {   // a wrap on the game's function: on the hook table at the first load only
        if (m.in_main_) {
            m.event_recs_.push_back(std::make_unique<ScriptMod::EventRec>(ScriptMod::EventRec{&m, e}));
            if (!m.api_->wrap(m.self_, kEventAddr[e], &event_trampoline, m.event_recs_.back().get()))
                return luaL_error(L, "b3.on(\"%s\"): the game's function %s cannot be wrapped (the log says why)", name, hex(kEventAddr[e]).c_str());
            m.installed_[e] = true;
        } else {
            m.log(B3MOD_LOG_WARN, std::string("b3.on(\"") + name + "\") after the game started: it is heard from the next start");
        }
    }
    append(L, event_list(e).c_str(), 2);
    if (!m.wants(e)) m.wants_.push_back(event_list(e));
    return 0;
}

void event_trampoline(B3ModHookCtx* ctx, const B3ModRegs* regs, B3ModResult* result, void* user) {
    auto* rec = static_cast<ScriptMod::EventRec*>(user);
    ScriptMod& m = *rec->m;
    if (m.dead_ || !m.L_ || !m.wants(rec->event)) {
        m.api_->next(ctx, regs, result);
        return;
    }
    const std::uint32_t a0 = std::uint32_t(regs->gpr[0]), a1 = std::uint32_t(regs->gpr[1]);
    // takedown: OnTakeDown(score, victim) counts one only when the victim was not already taken down; the count is the
    // score's +0x68 [L decomp B3Score_001553F0.cpp: field_68 + 1], its car +0xC8 [L same: victim +0x15CC = field_C8]
    const std::uint32_t before = rec->event == kTakedown ? m.u32(a0 + 0x68) : 0;
    m.api_->next(ctx, regs, result);
    if (m.dead_ || !m.L_) return;
    const std::uint32_t after = rec->event == kTakedown ? m.u32(a0 + 0x68) : 0;
    if (rec->event == kTakedown && after == before) return;
    m.run_list(event_list(rec->event), [&](lua_State* L) {
        switch (rec->event) {
        case kTakedown: {
            lua_createtable(L, 0, 6);
            const std::uint32_t car = m.u32(a0 + 0xC8);
            set_int(L, "car", car);
            set_int(L, "score", a0);
            set_int(L, "victim", a1);
            set_bool(L, "player", car == sa::kPlayerCar);
            set_bool(L, "victim_player", a1 == sa::kPlayerCar);
            set_int(L, "count", after);
            break;
        }
        case kCrash:
            lua_createtable(L, 0, 2);
            set_int(L, "car", a0);
            set_bool(L, "player", a0 == sa::kPlayerCar);
            break;
        default:   // kFinish
            lua_createtable(L, 0, 3);
            set_int(L, "car", a0);
            set_bool(L, "player", a0 == sa::kPlayerCar);
            set_int(L, "state", std::int32_t(a1));
            break;
        }
    });
    m.close_if_dead();
}

// ================================================================================================ b3.mem
struct Kind {
    const char* name;
    unsigned bytes;
    bool sign, flt;
};
const Kind kKinds[] = {{"u8", 1, false, false},  {"s8", 1, true, false},  {"u16", 2, false, false}, {"s16", 2, true, false},
                       {"u32", 4, false, false}, {"s32", 4, true, false}, {"f32", 4, false, true}};
const Kind& check_kind(lua_State* L, int i) {
    const char* k = luaL_checkstring(L, i);
    for (const Kind& x : kKinds)
        if (std::strcmp(k, x.name) == 0) return x;
    luaL_argerror(L, i, "a kind: u8, s8, u16, s16, u32, s32 or f32");
    return kKinds[0];
}
int l_mem_read(lua_State* L) {
    ScriptMod& m = self_of(L);
    const Kind& k = check_kind(L, 1);
    const std::uint32_t a = check_addr(L, 2);
    std::uint8_t b[4] = {};
    if (!m.rd(a, b, k.bytes)) {
        lua_pushnil(L);
        return 1;
    }
    if (k.flt) {
        float f;
        std::memcpy(&f, b, 4);
        lua_pushnumber(L, f);
        return 1;
    }
    std::uint32_t u = 0;
    std::memcpy(&u, b, k.bytes);   // the guest is little-endian, as every host the port runs on
    lua_Integer v = u;
    if (k.sign && ((u >> (8 * k.bytes - 1)) & 1u)) v -= lua_Integer(1) << (8 * k.bytes);
    lua_pushinteger(L, v);
    return 1;
}
int l_mem_write(lua_State* L) {
    ScriptMod& m = self_of(L);
    need_unsafe(L, "b3.mem.write");
    const Kind& k = check_kind(L, 1);
    const std::uint32_t a = check_addr(L, 2);
    std::uint8_t b[4] = {};
    if (k.flt) {
        const float f = static_cast<float>(luaL_checknumber(L, 3));
        std::memcpy(b, &f, 4);
    } else {
        const lua_Integer v = luaL_checkinteger(L, 3);
        const lua_Integer lo = -(lua_Integer(1) << (8 * k.bytes - 1)), hi = (lua_Integer(1) << (8 * k.bytes)) - 1;
        if (v < lo || v > hi) return luaL_argerror(L, 3, "does not fit the kind");
        const std::uint32_t u = static_cast<std::uint32_t>(v);
        std::memcpy(b, &u, k.bytes);
    }
    lua_pushboolean(L, m.api_->write(m.self_, a, b, k.bytes) != 0);
    return 1;
}
int l_mem_read_bytes(lua_State* L) {
    ScriptMod& m = self_of(L);
    const std::uint32_t a = check_addr(L, 1);
    const lua_Integer n = luaL_checkinteger(L, 2);
    if (n < 0 || n > (1 << 20)) return luaL_argerror(L, 2, "0 to 1048576 bytes");
    std::string buf(static_cast<std::size_t>(n), '\0');
    if (!m.rd(a, buf.data(), std::uint32_t(n))) {
        lua_pushnil(L);
        return 1;
    }
    lua_pushlstring(L, buf.data(), buf.size());
    return 1;
}
int l_mem_write_bytes(lua_State* L) {
    ScriptMod& m = self_of(L);
    need_unsafe(L, "b3.mem.write_bytes");
    const std::uint32_t a = check_addr(L, 1);
    std::size_t n = 0;
    const char* p = luaL_checklstring(L, 2, &n);
    if (n > (1u << 20)) return luaL_argerror(L, 2, "up to 1048576 bytes");
    lua_pushboolean(L, m.api_->write(m.self_, a, p, std::uint32_t(n)) != 0);
    return 1;
}

// ================================================================================================ b3.call, b3.hook
void push_result(lua_State* L, const B3ModResult& r) {
    float f;
    std::memcpy(&f, &r.f0, 4);
    lua_pushinteger(L, static_cast<lua_Integer>(r.v0));
    lua_pushnumber(L, f);
    lua_pushinteger(L, static_cast<lua_Integer>(r.v1));
}
int l_call(lua_State* L) {
    ScriptMod& m = self_of(L);
    const std::uint32_t a = check_addr(L, 1);
    if (m.in_main_ || m.reloading_) return luaL_error(L, "b3.call: not in the main chunk (the game has not started); call from a callback or a hook");
    need_unsafe(L, "b3.call");
    need_main_thread(L, "b3.call");
    if (std::find(m.cfg_.calls.begin(), m.cfg_.calls.end(), a) == m.cfg_.calls.end())
        return luaL_error(L, "b3.call: refused: %s is not in [script] calls (the game functions this mod may call)", hex(a).c_str());
    if (!m.api_->has_function(m.self_, a)) return luaL_error(L, "b3.call: no native function at %s", hex(a).c_str());
    B3ModRegs regs{};
    unsigned ng = 0, nf = 0;
    for (int i = 2; i <= lua_gettop(L); ++i) {
        if (lua_isinteger(L, i) || lua_isboolean(L, i)) {
            if (ng >= 8) return luaL_argerror(L, i, "more than 8 integer arguments");
            regs.gpr[ng++] = lua_isboolean(L, i) ? (lua_toboolean(L, i) ? 1u : 0u) : static_cast<std::uint64_t>(lua_tointeger(L, i));
        } else if (lua_type(L, i) == LUA_TNUMBER) {
            if (nf >= 8) return luaL_argerror(L, i, "more than 8 float arguments");
            const float f = static_cast<float>(lua_tonumber(L, i));
            std::memcpy(&regs.fpr[nf++], &f, 4);
        } else {
            return luaL_argerror(L, i, "an integer, a float or a boolean");
        }
    }
    B3ModResult r{};
    game_code(L, m, [&] { m.api_->call(m.self_, a, &regs, &r); });
    push_result(L, r);
    return 3;
}

CallState& check_call(lua_State* L) {
    auto** p = static_cast<CallState**>(luaL_checkudata(L, 1, "b3.call"));
    if (!*p || self_of(L).dead_) luaL_error(L, "this call has returned (a call object is valid only inside its hook)");
    return **p;
}
int check_reg(lua_State* L, int i) {
    const lua_Integer k = luaL_checkinteger(L, i);
    if (k < 1 || k > 8) luaL_argerror(L, i, "1 to 8");
    return int(k) - 1;
}
int lc_arg(lua_State* L) {
    CallState& c = check_call(L);
    lua_pushinteger(L, static_cast<lua_Integer>(c.regs.gpr[check_reg(L, 2)]));
    return 1;
}
int lc_farg(lua_State* L) {
    CallState& c = check_call(L);
    float f;
    std::memcpy(&f, &c.regs.fpr[check_reg(L, 2)], 4);
    lua_pushnumber(L, f);
    return 1;
}
int lc_sp(lua_State* L) {
    lua_pushinteger(L, check_call(L).regs.sp);
    return 1;
}
int lc_set_arg(lua_State* L) {
    CallState& c = check_call(L);
    need_unsafe(L, "call:set_arg");   // (review: an argument is often a pointer or an index the game's code follows)
    const int r = check_reg(L, 2);
    c.regs.gpr[r] = static_cast<std::uint64_t>(luaL_checkinteger(L, 3));
    return 0;
}
int lc_set_farg(lua_State* L) {
    CallState& c = check_call(L);
    need_gameplay(L, "call:set_farg");
    const int r = check_reg(L, 2);
    const float f = static_cast<float>(luaL_checknumber(L, 3));
    std::memcpy(&c.regs.fpr[r], &f, 4);
    return 0;
}
int lc_next(lua_State* L) {
    CallState& c = check_call(L);
    ScriptMod& m = self_of(L);
    if (c.next_called) return luaL_error(L, "call:next() twice");
    need_main_thread(L, "call:next");
    c.next_called = true;
    game_code(L, m, [&] { m.api_->next(c.ctx, &c.regs, &c.result); });
    push_result(L, c.result);
    return 3;
}
int lc_result(lua_State* L) {
    push_result(L, check_call(L).result);
    return 3;
}
int lc_set_result(lua_State* L) {
    CallState& c = check_call(L);
    need_unsafe(L, "call:set_result");   // (a result is often a pointer or an index)
    c.result.v0 = static_cast<std::uint64_t>(luaL_checkinteger(L, 2));
    c.result.v0_hi = 0;
    if (!lua_isnoneornil(L, 3)) {
        const float f = static_cast<float>(luaL_checknumber(L, 3));
        std::memcpy(&c.result.f0, &f, 4);
    }
    return 0;
}
int lc_set_fresult(lua_State* L) {
    CallState& c = check_call(L);
    need_gameplay(L, "call:set_fresult");
    const float f = static_cast<float>(luaL_checknumber(L, 2));
    std::memcpy(&c.result.f0, &f, 4);
    return 0;
}

void hook_trampoline(B3ModHookCtx* ctx, const B3ModRegs* regs, B3ModResult* result, void* user) {
    auto* h = static_cast<ScriptMod::HookRec*>(user);
    ScriptMod& m = *h->m;
    if (m.dead_ || !m.L_ || !h->bound) {   // stopped, or no function in this state: the game's call as without the mod
        m.api_->next(ctx, regs, result);
        return;
    }
    CallState c{ctx, *regs, *result, false, h->replace};
    const bool ok = m.protect([&](lua_State* L) {
        lua_rawgetp(L, LUA_REGISTRYINDEX, &kHooksKey);
        lua_rawgeti(L, -1, h->slot);   // [hooks] [fn]
        auto** ud = static_cast<CallState**>(lua_newuserdatauv(L, sizeof(CallState*), 0));
        *ud = &c;
        luaL_setmetatable(L, "b3.call");
        lua_pushvalue(L, -1);
        lua_insert(L, -3);   // [hooks] [call] [fn] [call]: kept on the stack through the call
        lua_call(L, 1, 0);
        *ud = nullptr;   // the object is no use after its call
    });
    if (!ok) {   // stopped inside: the game's own call with the game's arguments, unless it already ran
        if (!c.next_called) m.api_->next(ctx, regs, result);
        else *result = c.result;
        m.close_if_dead();
        return;
    }
    if (!c.next_called && !h->replace) m.api_->next(ctx, &c.regs, &c.result);   // a wrap always runs the game's call
    *result = c.result;
}

int l_hook(lua_State* L, bool replace) {
    ScriptMod& m = self_of(L);
    const char* what = replace ? "b3.hook.replace" : "b3.hook.wrap";
    need_main_chunk(L, what);
    if (replace) need_unsafe(L, what);   // (review: skips the game's function and hands back a chosen v0)
    const std::uint32_t a = check_addr(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    ScriptMod::HookRec* rec = nullptr;
    if (m.reloading_) {   // hot reload: the same hook as at the first load gets the new function
        for (const auto& h : m.hooks_)
            if (h->addr == a && h->replace == replace && !h->bound) {
                rec = h.get();
                break;
            }
        if (!rec) {
            m.log(B3MOD_LOG_WARN, std::string(what) + " " + hex(a) + " is new since the game started: it is used from the next start");
            return 0;
        }
    } else {
        if (!m.api_->has_function(m.self_, a)) return luaL_error(L, "%s: no native function at %s", what, hex(a).c_str());
        m.hooks_.push_back(std::make_unique<ScriptMod::HookRec>(ScriptMod::HookRec{&m, a, replace, int(m.hooks_.size()) + 1, false}));
        rec = m.hooks_.back().get();
        const int ok = replace ? m.api_->replace(m.self_, a, &hook_trampoline, rec) : m.api_->wrap(m.self_, a, &hook_trampoline, rec);
        if (!ok) return luaL_error(L, "%s %s: refused (the log says why)", what, hex(a).c_str());
    }
    lua_rawgetp(L, LUA_REGISTRYINDEX, &kHooksKey);
    lua_pushvalue(L, 2);
    lua_rawseti(L, -2, rec->slot);
    lua_pop(L, 1);
    rec->bound = true;
    return 0;
}
int l_hook_wrap(lua_State* L) { return l_hook(L, false); }
int l_hook_replace(lua_State* L) { return l_hook(L, true); }

// ================================================================================================ b3.settings
int l_settings_add(lua_State* L) {
    ScriptMod& m = self_of(L);
    need_main_chunk(L, "b3.settings.add");
    luaL_checktype(L, 1, LUA_TTABLE);
    auto field = [&](const char* k) -> std::string {
        std::string v;
        if (lua_getfield(L, 1, k) == LUA_TSTRING) v = lua_tostring(L, -1);
        else if (!lua_isnil(L, -1)) luaL_error(L, "b3.settings.add: %s is not a string", k);
        lua_pop(L, 1);
        return v;
    };
    const std::string key = field("key"), label = field("label"), description = field("description");
    std::string kind = field("kind");
    if (kind.empty()) kind = "toggle";
    if (key.empty()) return luaL_error(L, "b3.settings.add: no key");
    ScriptMod::Row row{key, -1, {}, 0};
    std::uint32_t def = 0;
    lua_getfield(L, 1, "default");
    const int dt = lua_gettop(L);
    if (kind == "toggle") {
        row.kind = B3MOD_SETTING_TOGGLE;
        row.count = 2;
        if (!lua_isnil(L, dt)) def = lua_toboolean(L, dt) ? 1 : 0;
    } else if (kind == "choice") {
        row.kind = B3MOD_SETTING_CHOICE;
        if (lua_getfield(L, 1, "values") != LUA_TTABLE) return luaL_error(L, "b3.settings.add %s: a choice needs values = { \"name\", ... }", key.c_str());
        const lua_Integer n = luaL_len(L, -1);
        for (lua_Integer i = 1; i <= n && i <= 64; ++i) {
            lua_rawgeti(L, -1, i);
            if (lua_type(L, -1) != LUA_TSTRING) return luaL_error(L, "b3.settings.add %s: values[%d] is not a string", key.c_str(), int(i));
            row.values.emplace_back(lua_tostring(L, -1));
            lua_pop(L, 1);
        }
        lua_pop(L, 1);
        row.count = unsigned(row.values.size());
        if (lua_type(L, dt) == LUA_TSTRING) {
            const auto it = std::find(row.values.begin(), row.values.end(), std::string(lua_tostring(L, dt)));
            if (it == row.values.end()) return luaL_error(L, "b3.settings.add %s: default is not one of its values", key.c_str());
            def = std::uint32_t(it - row.values.begin());
        } else if (!lua_isnil(L, dt)) {
            return luaL_error(L, "b3.settings.add %s: a choice's default is one of its values (a name)", key.c_str());
        }
    } else if (kind == "slider") {
        row.kind = B3MOD_SETTING_SLIDER;
        lua_getfield(L, 1, "count");
        row.count = unsigned(luaL_optinteger(L, -1, 0));
        lua_pop(L, 1);
        if (!lua_isnil(L, dt)) def = std::uint32_t(luaL_checkinteger(L, dt));
    } else {
        return luaL_error(L, "b3.settings.add %s: kind \"%s\" is not toggle, choice or slider", key.c_str(), kind.c_str());
    }
    lua_settop(L, 1);
    for (const ScriptMod::Row& r : m.rows_)
        if (r.key == key) {
            if (m.reloading_) return 0;   // hot reload: the first load's row stays
            return luaL_error(L, "b3.settings.add: %s added twice", key.c_str());
        }
    if (m.reloading_) {
        m.log(B3MOD_LOG_WARN, "b3.settings.add " + key + " is new since the game started: it is there from the next start");
        return 0;
    }
    std::vector<const char*> names;
    for (const std::string& v : row.values) names.push_back(v.c_str());
    B3ModSetting st{};
    st.size = sizeof st;
    st.kind = std::uint32_t(row.kind);
    st.key = key.c_str();
    st.label = label.c_str();
    st.description = description.empty() ? nullptr : description.c_str();
    st.values = names.empty() ? nullptr : names.data();
    st.count = row.count;
    st.default_value = def;
    if (!m.api_->add_setting(m.self_, &st)) return luaL_error(L, "b3.settings.add %s: refused (the log says why)", key.c_str());
    m.rows_.push_back(std::move(row));
    return 0;
}
// a toggle: true / false; a choice: its value's name; a slider: its level (0 ..)
int l_settings_get(lua_State* L) {
    ScriptMod& m = self_of(L);
    const char* key = luaL_checkstring(L, 1);
    const int v = m.api_->get_setting(m.self_, key);
    const ScriptMod::Row* row = nullptr;
    for (const ScriptMod::Row& r : m.rows_)
        if (r.key == key) row = &r;
    if (v < 0 || !row) {
        lua_pushnil(L);
        return 1;
    }
    if (row->kind == B3MOD_SETTING_TOGGLE) lua_pushboolean(L, v != 0);
    else if (row->kind == B3MOD_SETTING_CHOICE && std::size_t(v) < row->values.size()) lua_pushstring(L, row->values[std::size_t(v)].c_str());
    else lua_pushinteger(L, v);
    return 1;
}
int l_settings_index(lua_State* L) {
    ScriptMod& m = self_of(L);
    const int v = m.api_->get_setting(m.self_, luaL_checkstring(L, 1));
    if (v < 0) lua_pushnil(L);
    else lua_pushinteger(L, v);
    return 1;
}

// ================================================================================================ b3.draw
float num(lua_State* L, int i) { return static_cast<float>(luaL_checknumber(L, i)); }
std::uint32_t colour(lua_State* L, int i) {
    const lua_Integer c = luaL_optinteger(L, i, lua_Integer(0xFFFFFFFFu));
    if (c < 0 || c > 0xFFFFFFFFll) luaL_argerror(L, i, "a colour (b3.rgba)");
    return static_cast<std::uint32_t>(c);
}
int l_draw_quad(lua_State* L) {
    ScriptMod& m = self_of(L);
    const float x = num(L, 1), y = num(L, 2), w = num(L, 3), h = num(L, 4);
    const std::uint32_t c = colour(L, 5);
    const lua_Integer tex = luaL_optinteger(L, 6, 0);
    m.api_->quad(m.self_, x, y, w, h, c, static_cast<std::uint32_t>(tex));
    return 0;
}
int l_draw_text(lua_State* L) {
    ScriptMod& m = self_of(L);
    const float x = num(L, 1), y = num(L, 2), size = num(L, 3);
    const std::uint32_t c = colour(L, 4);
    const char* s = luaL_tolstring(L, 5, nullptr);
    m.api_->text(m.self_, x, y, size, c, s);
    return 0;
}
int l_draw_texture(lua_State* L) {
    ScriptMod& m = self_of(L);
    const lua_Integer w = luaL_checkinteger(L, 1), h = luaL_checkinteger(L, 2);
    std::size_t n = 0;
    const char* px = luaL_checklstring(L, 3, &n);
    if (w < 1 || h < 1 || w > 1024 || h > 1024) return luaL_argerror(L, 1, "1 to 1024 pixels each way");
    if (n != std::size_t(w * h * 4)) return luaL_argerror(L, 3, "width x height x 4 bytes (RGBA)");
    const std::uint32_t id = m.api_->texture(m.self_, std::uint32_t(w), std::uint32_t(h), reinterpret_cast<const std::uint8_t*>(px));
    if (!id) return luaL_error(L, "b3.draw.texture: refused");
    lua_pushinteger(L, id);
    return 1;
}

// ================================================================================================ b3.data, require, load
// up to n bytes of `path` from `offset` (to its end when n < 0) pushed as a string; false (nothing pushed): no file
bool push_data(lua_State* L, const char* path, std::int64_t off, std::int64_t n) {
    ScriptMod& m = self_of(L);
    if (m.snapped_) {   // an unsafe script's own files: the bytes read and hashed at load
        if (const std::string* s = m.snapped(path)) {
            const std::size_t o = std::size_t(std::clamp<std::int64_t>(off, 0, std::int64_t(s->size())));
            const std::size_t k = n < 0 ? s->size() - o : std::min<std::size_t>(std::size_t(n), s->size() - o);
            lua_pushlstring(L, s->data() + o, k);
            return true;
        }
    }
    const std::int64_t size = m.api_->data_size(m.self_, path);
    if (size < 0) return false;
    if (off < 0) off = 0;
    std::int64_t want = std::max<std::int64_t>(0, size - off);
    if (n >= 0) want = std::min(want, n);
    if (std::uint64_t(want) > m.cap_) luaL_error(L, "%s: %I bytes is more than the script's memory", path, lua_Integer(want));
    std::string buf(std::size_t(want), '\0');
    const std::int64_t got = want ? m.api_->read_data(m.self_, path, std::uint64_t(off), buf.data(), std::uint64_t(want)) : 0;
    if (got < 0) return false;
    lua_pushlstring(L, buf.data(), std::size_t(got));
    return true;
}
int l_data_size(lua_State* L) {
    ScriptMod& m = self_of(L);
    const std::int64_t n = m.api_->data_size(m.self_, luaL_checkstring(L, 1));
    if (n < 0) lua_pushnil(L);
    else lua_pushinteger(L, n);
    return 1;
}
int l_data_read(lua_State* L) {
    const char* path = luaL_checkstring(L, 1);
    if (!push_data(L, path, luaL_optinteger(L, 2, 0), luaL_optinteger(L, 3, -1))) lua_pushnil(L);
    return 1;
}
// b3.data.open(path): a table {path, pos} whose metatable ("b3.file") has read / seek / tell / size
int l_file_read(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    const lua_Integer want = luaL_optinteger(L, 2, -1);
    lua_settop(L, 1);
    lua_getfield(L, 1, "path");
    lua_getfield(L, 1, "pos");
    const std::string path = luaL_checkstring(L, -2);
    const lua_Integer pos = luaL_checkinteger(L, -1);
    lua_pop(L, 2);
    if (!push_data(L, path.c_str(), pos, want)) return luaL_error(L, "%s: the file is gone", path.c_str());
    const std::size_t n = lua_rawlen(L, -1);
    if (n == 0 && want != 0) {   // at the end: nil
        lua_pop(L, 1);
        lua_pushnil(L);
        return 1;
    }
    lua_pushinteger(L, pos + lua_Integer(n));
    lua_setfield(L, 1, "pos");
    return 1;
}
int l_file_seek(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_pushinteger(L, std::max<lua_Integer>(0, luaL_checkinteger(L, 2)));
    lua_setfield(L, 1, "pos");
    return 0;
}
int l_file_tell(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_getfield(L, 1, "pos");
    return 1;
}
int l_file_size(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_getfield(L, 1, "path");
    ScriptMod& m = self_of(L);
    const std::int64_t n = m.api_->data_size(m.self_, luaL_checkstring(L, -1));
    if (n < 0) lua_pushnil(L);
    else lua_pushinteger(L, n);
    return 1;
}
int l_data_open(lua_State* L) {
    ScriptMod& m = self_of(L);
    const char* path = luaL_checkstring(L, 1);
    if (m.api_->data_size(m.self_, path) < 0) {
        lua_pushnil(L);
        lua_pushfstring(L, "%s: no such file (disc:<path> for a disc file, else a path in the mod's folder)", path);
        return 2;
    }
    lua_createtable(L, 0, 2);
    lua_pushstring(L, path);
    lua_setfield(L, -2, "path");
    lua_pushinteger(L, 0);
    lua_setfield(L, -2, "pos");
    luaL_setmetatable(L, "b3.file");
    return 1;
}
// require(name): <script folder>/<name, '.' as '/'>.lua, loaded once
int l_require(lua_State* L) {
    ScriptMod& m = self_of(L);
    const char* name = luaL_checkstring(L, 1);
    std::string rel = folder_of(m.cfg_.main_file) + "/";
    if (!*name || name[0] == '.' || std::strstr(name, "..") || name[std::strlen(name) - 1] == '.') return luaL_argerror(L, 1, "not a module name");
    for (const char* p = name; *p; ++p) {
        const char c = *p;
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.'))
            return luaL_argerror(L, 1, "a module name: letters, digits, '_', '-', and '.' between folders");
        rel += c == '.' ? '/' : c;
    }
    rel += ".lua";
    lua_settop(L, 1);
    lua_rawgetp(L, LUA_REGISTRYINDEX, &kLoadedKey);   // 2
    if (lua_getfield(L, 2, name) != LUA_TNIL) {
        if (lua_islightuserdata(L, -1)) return luaL_error(L, "require \"%s\": it requires itself (a loop)", name);
        return 1;
    }
    lua_pop(L, 1);
    if ((m.snapped_ && !m.snapped(rel)) || !push_data(L, rel.c_str(), 0, -1)) return luaL_error(L, "require \"%s\": no file %s in the mod", name, rel.c_str());
    std::size_t n = 0;
    const char* text = lua_tolstring(L, 3, &n);
    if (luaL_loadbufferx(L, text, n, ("@" + rel).c_str(), "t") != LUA_OK) return lua_error(L);   // 4
    lua_pushlightuserdata(L, &kLoadedKey);   // "being loaded"
    lua_setfield(L, 2, name);
    lua_pushstring(L, name);
    lua_call(L, 1, 1);   // 4: its value
    if (lua_isnil(L, 4)) {
        lua_pop(L, 1);
        lua_pushboolean(L, 1);
    }
    lua_pushvalue(L, 4);
    lua_setfield(L, 2, name);
    return 1;
}
// load(chunk [, chunkname [, mode [, env]]]): text only (a precompiled chunk can break the interpreter)
int l_load(lua_State* L) {
    const int n = lua_gettop(L);
    lua_pushvalue(L, lua_upvalueindex(1));
    lua_pushvalue(L, 1);
    if (n >= 2) lua_pushvalue(L, 2);
    else lua_pushnil(L);
    lua_pushliteral(L, "t");
    if (n >= 4) lua_pushvalue(L, 4);
    lua_call(L, n >= 4 ? 4 : 3, LUA_MULTRET);
    return lua_gettop(L) - n;
}
// string.rep(s, n [, sep]): the result no larger than the script's memory, and "" at once when it would be empty (the
// library's own loop would copy nothing 2^63 times: string.rep("", math.maxinteger) never returns)
int l_rep(lua_State* L) {
    std::size_t l = 0, lsep = 0;
    luaL_checklstring(L, 1, &l);
    const lua_Integer n = luaL_checkinteger(L, 2);
    luaL_optlstring(L, 3, "", &lsep);
    if (n <= 0 || l + lsep == 0) {
        lua_pushliteral(L, "");
        return 1;
    }
    const long double total = (long double)l * (long double)n + (long double)lsep * (long double)(n - 1);
    if (total > (long double)self_of(L).cap_) {
        char b[96];
        std::snprintf(b, sizeof b, "string.rep: a result of %.0Lf bytes is more than the script's memory", total);
        return luaL_error(L, "%s", b);
    }
    lua_pushvalue(L, lua_upvalueindex(1));
    lua_insert(L, 1);
    lua_call(L, lua_gettop(L) - 1, 1);
    return 1;
}
int l_randomseed(lua_State* L) {
    if (lua_gettop(L) == 0)
        return luaL_error(L, "math.randomseed: give it a seed (there is no clock in a script; math.random starts seeded from the mod's id)");
    const int n = lua_gettop(L);
    lua_pushvalue(L, lua_upvalueindex(1));
    lua_insert(L, 1);
    lua_call(L, n, LUA_MULTRET);
    return lua_gettop(L);
}

// ================================================================================================ b3.view
int l_view_race(lua_State* L) {
    ScriptMod& m = self_of(L);
    const std::uint32_t game = m.u32(sa::kGamePtr);
    const std::uint32_t logic = game ? m.u32(game + 0x1B4) : 0;
    const std::uint32_t vt = logic ? m.u32(logic) : 0;
    lua_createtable(L, 0, 8);
    set_bool(L, "active", m.in_race());
    set_num(L, "timer", m.f32(sa::kTimer));
    set_int(L, "ticks_per_frame", lua_Integer(m.u32(sa::kTicksPerFrame)));
    set_int(L, "cars", lua_Integer(m.u32(sa::kRaceCarCount)));
    set_bool(L, "split_screen", game == sa::kSplitGame);
    set_int(L, "logic", lua_Integer(vt));
    set_bool(L, "time_attack", vt == sa::kTimeAttackLogic);
    set_int(L, "vblank", lua_Integer(m.vblank()));
    return 1;
}
int l_view_player(lua_State* L) {
    ScriptMod& m = self_of(L);
    if (!m.in_race()) {
        lua_pushnil(L);
        return 1;
    }
    float pos[3] = {};
    m.rd(sa::kPlayerCar + 0x30, pos, 12);
    const float raw = m.f32(sa::kSpeedVector + 0xC);
    const float factor = m.f32(m.u32(sa::kSpeedUnits) > 1 ? sa::kSpeedFactorHigh : sa::kSpeedFactorLow);
    lua_createtable(L, 0, 8);
    set_int(L, "car", sa::kPlayerCar);
    set_num(L, "x", pos[0]);
    set_num(L, "y", pos[1]);
    set_num(L, "z", pos[2]);
    set_num(L, "speed", raw);
    set_num(L, "speed_factor", factor);
    set_int(L, "speed_shown", static_cast<lua_Integer>(factor * raw));   // as the speedometer: (int)(factor x speed)
    set_int(L, "speed_units", lua_Integer(m.u32(sa::kSpeedUnits)));
    return 1;
}
int l_view_score(lua_State* L) {
    ScriptMod& m = self_of(L);
    if (!m.in_race()) {
        lua_pushnil(L);
        return 1;
    }
    const std::uint32_t sc = sa::kPlayerScore;
    lua_createtable(L, 0, 12);
    set_int(L, "state", std::int32_t(m.u32(sc + 0x27C)));
    set_int(L, "finish", std::int32_t(m.u32(sc + 0x8)));
    set_num(L, "time", m.f32(sc + 0xC));
    set_int(L, "lap", std::int32_t(m.u32(sc + 0x14)));
    set_int(L, "laps_done", std::int32_t(m.u32(sc + 0x18)));
    set_int(L, "laps", std::int32_t(m.u32(sc + 0x2C4)));
    set_num(L, "lap_time", m.f32(sc + 0x2C8));
    set_num(L, "best_lap", m.f32(sc + 0x3C));
    set_int(L, "takedowns", std::int32_t(m.u32(sc + 0x68)));
    lua_createtable(L, 8, 0);
    for (int k = 0; k < 8; ++k) {
        lua_pushnumber(L, m.f32(sc + 0x1C + 4 * std::uint32_t(k)));
        lua_rawseti(L, -2, k + 1);
    }
    lua_setfield(L, -2, "lap_times");
    return 1;
}

// ================================================================================================ the state
void setup(lua_State* L) {
    ScriptMod& m = self_of(L);
    // the libraries a script may have
    const std::pair<const char*, lua_CFunction> libs[] = {{LUA_GNAME, luaopen_base},       {LUA_COLIBNAME, luaopen_coroutine},
                                                          {LUA_MATHLIBNAME, luaopen_math}, {LUA_STRLIBNAME, luaopen_string},
                                                          {LUA_TABLIBNAME, luaopen_table}, {LUA_UTF8LIBNAME, luaopen_utf8}};
    for (const auto& [name, fn] : libs) {
        luaL_requiref(L, name, fn, 1);
        lua_pop(L, 1);
    }
    lua_newtable(L);
    lua_rawsetp(L, LUA_REGISTRYINDEX, &kListsKey);
    lua_newtable(L);
    lua_rawsetp(L, LUA_REGISTRYINDEX, &kLoadedKey);
    lua_newtable(L);
    lua_rawsetp(L, LUA_REGISTRYINDEX, &kHooksKey);
    // the sandbox: no files but b3.data and require, no precompiled chunks
    for (const char* g : {"dofile", "loadfile"}) {
        lua_pushnil(L);
        lua_setglobal(L, g);
    }
    lua_getglobal(L, "string");
    lua_pushnil(L);
    lua_setfield(L, -2, "dump");
    lua_getfield(L, -1, "rep");
    lua_pushcclosure(L, &guarded<l_rep>, 1);
    lua_setfield(L, -2, "rep");
    lua_pop(L, 1);
    lua_getglobal(L, "load");
    lua_pushcclosure(L, &guarded<l_load>, 1);
    lua_setglobal(L, "load");
    lua_pushcfunction(L, &guarded<l_require>);
    lua_setglobal(L, "require");
    lua_pushcfunction(L, &guarded<l_log>);
    lua_setglobal(L, "print");
    // math.random: seeded from the mod's id (FNV-1a 64 of it), the same in every run; randomseed() needs a seed
    lua_getglobal(L, "math");
    lua_getfield(L, -1, "randomseed");
    lua_pushvalue(L, -1);
    const std::uint64_t seed = b3mod::fnv1a64(m.cfg_.id.data(), m.cfg_.id.size());
    lua_pushinteger(L, static_cast<lua_Integer>(seed));
    lua_call(L, 1, 0);
    lua_pushcclosure(L, &guarded<l_randomseed>, 1);
    lua_setfield(L, -2, "randomseed");
    lua_pop(L, 1);
    // the call object's and the file's metatables
    luaL_newmetatable(L, "b3.call");
    const luaL_Reg call_methods[] = {{"arg", &guarded<lc_arg>},       {"farg", &guarded<lc_farg>},     {"set_arg", &guarded<lc_set_arg>},       {"set_farg", &guarded<lc_set_farg>},
                                     {"next", &guarded<lc_next>},     {"result", &guarded<lc_result>}, {"set_result", &guarded<lc_set_result>}, {"set_fresult", &guarded<lc_set_fresult>},
                                     {"sp", &guarded<lc_sp>},         {nullptr, nullptr}};
    luaL_newlib(L, call_methods);
    lua_setfield(L, -2, "__index");
    lua_pushliteral(L, "b3.call");
    lua_setfield(L, -2, "__metatable");
    lua_pop(L, 1);
    luaL_newmetatable(L, "b3.file");
    const luaL_Reg file_methods[] = {{"read", &guarded<l_file_read>}, {"seek", &guarded<l_file_seek>}, {"tell", &guarded<l_file_tell>}, {"size", &guarded<l_file_size>}, {nullptr, nullptr}};
    luaL_newlib(L, file_methods);
    lua_setfield(L, -2, "__index");
    lua_pop(L, 1);
    // b3
    const luaL_Reg b3[] = {{"log", &guarded<l_log>},         {"warn", &guarded<l_warn>},           {"vblank", &guarded<l_vblank>},     {"has_function", &guarded<l_has_function>},
                           {"rgba", &guarded<l_rgba>},       {"on_vblank", &guarded<l_on_vblank>}, {"on_frame", &guarded<l_on_frame>}, {"on_shutdown", &guarded<l_on_shutdown>},
                           {"on_load", &guarded<l_on_load>}, {"on", &guarded<l_on>},               {"call", &guarded<l_call>},         {nullptr, nullptr}};
    luaL_newlib(L, b3);
    lua_pushstring(L, m.cfg_.id.c_str());
    lua_setfield(L, -2, "id");
    lua_pushstring(L, m.cfg_.version.c_str());
    lua_setfield(L, -2, "version");
    lua_pushstring(L, m.api_->port_version());
    lua_setfield(L, -2, "port_version");
    lua_pushboolean(L, m.cfg_.gameplay);
    lua_setfield(L, -2, "gameplay");
    lua_pushboolean(L, m.cfg_.unsafe);
    lua_setfield(L, -2, "unsafe");
    lua_pushboolean(L, m.cfg_.developer);
    lua_setfield(L, -2, "developer");
    auto sub = [&](const char* name, const luaL_Reg* fns) {
        lua_newtable(L);
        luaL_setfuncs(L, fns, 0);
        lua_setfield(L, -2, name);
    };
    const luaL_Reg mem[] = {{"read", &guarded<l_mem_read>}, {"write", &guarded<l_mem_write>}, {"read_bytes", &guarded<l_mem_read_bytes>}, {"write_bytes", &guarded<l_mem_write_bytes>}, {nullptr, nullptr}};
    const luaL_Reg hook[] = {{"wrap", &guarded<l_hook_wrap>}, {"replace", &guarded<l_hook_replace>}, {nullptr, nullptr}};
    const luaL_Reg settings[] = {{"add", &guarded<l_settings_add>}, {"get", &guarded<l_settings_get>}, {"index", &guarded<l_settings_index>}, {nullptr, nullptr}};
    const luaL_Reg draw[] = {{"quad", &guarded<l_draw_quad>}, {"text", &guarded<l_draw_text>}, {"texture", &guarded<l_draw_texture>}, {nullptr, nullptr}};
    const luaL_Reg data[] = {{"size", &guarded<l_data_size>}, {"read", &guarded<l_data_read>}, {"open", &guarded<l_data_open>}, {nullptr, nullptr}};
    const luaL_Reg view[] = {{"race", &guarded<l_view_race>}, {"player", &guarded<l_view_player>}, {"score", &guarded<l_view_score>}, {nullptr, nullptr}};
    sub("mem", mem);
    sub("hook", hook);
    sub("settings", settings);
    sub("draw", draw);
    sub("data", data);
    sub("view", view);
    lua_setglobal(L, "b3");
}

bool ScriptMod::open_state() {
    close();
    dead_ = over_ = mem_hit_ = timed_out_ = false;
    wants_.clear();
    cap_ = std::size_t(cfg_.memory);
    L_ = lua_newstate(&lua_alloc, this);
    if (!L_) {
        log(B3MOD_LOG_ERROR, "the script did not load: no memory for a Lua state");
        dead_ = true;
        return false;
    }
    *static_cast<ScriptMod**>(lua_getextraspace(L_)) = this;
    lua_setwarnf(L_, &warn_fn, this);
    lua_sethook(L_, &count_hook, LUA_MASKCOUNT, kStep);
    return protect([](lua_State* L) { setup(L); });
}

void ScriptMod::stop(int rc, const std::string& msg) {
    if (dead_) return;
    dead_ = true;
    const bool loading = in_main_ || reloading_;
    std::string head;
    if (fatal_) {
        head = "a fatal error in the game's code it called (b3.call or call:next): " + fatal_what_ + "; the error goes on to the game";
    } else if (timed_out_) {
        head = "it ran for more than " + std::to_string(time_limit() / 1000000) + " ms" + (loading ? " while loading" : " in one VBlank") +
               " (its time limit: a string function or pattern that does not end?)";
    } else if (over_) {
        head = loading ? "its main chunk ran past " + std::to_string(cfg_.budget * kScriptLoadBudgetFactor) + " Lua instructions (its load budget)"
                       : "it ran past " + std::to_string(cfg_.budget) + " Lua instructions in one VBlank (its budget: a loop that does not end?)";
    } else if (mem_hit_ || rc == LUA_ERRMEM) {
        head = "it used more than " + std::to_string(cap_ >> 20) + " MB (its memory cap)";
    } else {
        head = "an error";
    }
    // the message's first line, then the traceback's lines
    std::vector<std::string> lines;
    for (std::size_t at = 0; at <= msg.size();) {
        const std::size_t nl = msg.find('\n', at);
        std::string l = msg.substr(at, nl == std::string::npos ? std::string::npos : nl - at);
        while (!l.empty() && (l.front() == '\t' || l.front() == ' ')) l.erase(0, 1);
        if (!l.empty() && l != "stack traceback:" && l.find("[C]: in ?") == std::string::npos) lines.push_back(l);
        if (nl == std::string::npos) break;
        at = nl + 1;
    }
    CodeMods::release_textures(self_);   // modsec: its pictures go with it (a reload makes its own again)
    if (loading) log(B3MOD_LOG_ERROR, "the script did not load: " + head);
    else log(B3MOD_LOG_ERROR, "the script was stopped at VBlank " + std::to_string(vblank()) + ": " + head + (fatal_ ? "" : "; the game goes on without it"));
    for (std::size_t i = 0; i < lines.size() && i < 8; ++i) log(B3MOD_LOG_ERROR, "  " + lines[i]);
    if (cfg_.developer) log(B3MOD_LOG_INFO, "it loads again when its script changes (hot reload, Developer mods)");
}

// the script's text, from the loader's file API (the mod's own folder); false with a log line when it is not there
bool read_script(ScriptMod& m, std::string& text) {
    if (m.snapped_) {
        const std::string* s = m.snapped(m.cfg_.main_file);
        if (!s) {
            m.log(B3MOD_LOG_ERROR, "the script did not load: no file " + m.cfg_.main_file + " in the mod");
            return false;
        }
        text = *s;
        return true;
    }
    const std::int64_t size = m.api_->data_size(m.self_, m.cfg_.main_file.c_str());
    if (size < 0) {
        m.log(B3MOD_LOG_ERROR, "the script did not load: no file " + m.cfg_.main_file + " in the mod");
        return false;
    }
    text.assign(std::size_t(size), '\0');
    if (size && m.api_->read_data(m.self_, m.cfg_.main_file.c_str(), 0, text.data(), std::uint64_t(size)) != size) {
        m.log(B3MOD_LOG_ERROR, "the script did not load: " + m.cfg_.main_file + " could not be read");
        return false;
    }
    return true;
}

bool ScriptMod::run_main(const std::string& text) {
    return protect([&](lua_State* L) {
        if (luaL_loadbufferx(L, text.data(), text.size(), ("@" + cfg_.main_file).c_str(), "t") != LUA_OK) lua_error(L);
        lua_call(L, 0, 0);
        // on_load(reloaded): where a script builds its state
        push_list(L, "load");
        const int list = lua_gettop(L);
        const lua_Integer n = lua_Integer(lua_rawlen(L, list));
        for (lua_Integer i = 1; i <= n; ++i) {
            lua_rawgeti(L, list, i);
            lua_pushboolean(L, reloading_);
            lua_call(L, 1, 0);
        }
    });
}

// An unsafe script's files, read once (b3mod::trust_files: mod.ini and the script folder); check: they must hash to the
// set the user allowed (cfg_.trust_digest, when given)
bool ScriptMod::snapshot(bool check) {
    snap_.clear();
    snapped_ = false;
    if (!cfg_.unsafe) return true;
    b3mod::ScriptSpec sp;
    sp.present = sp.unsafe = true;
    sp.main_file = cfg_.main_file;
    std::vector<b3mod::TrustFile> files;
    std::string why;
    if (!b3mod::trust_files(cfg_.dir, b3mod::CodeSpec{}, sp, files, &why)) {
        log(B3MOD_LOG_ERROR, "the script did not load: its files cannot be read (" + why + ")");
        return false;
    }
    if (check && !cfg_.trust_digest.empty() && b3mod::trust_digest(files) != cfg_.trust_digest) {
        log(B3MOD_LOG_ERROR, "the script did not load: its files changed after they were checked against what you allowed (start the game again)");
        return false;
    }
    for (b3mod::TrustFile& f : files) snap_[f.rel] = std::move(f.bytes);
    // review: what the script may do is read from the mod.ini that was hashed, not the one parsed before (a file
    // changed between the two would otherwise hand the checked script other rights)
    const auto ini = snap_.find(b3mod::kManifestName);
    const b3mod::ParsedManifest pm = b3mod::parse_manifest(ini == snap_.end() ? std::string_view() : std::string_view(ini->second));
    if (!pm.errors.empty() || !pm.m.script.present || pm.m.script.main_file != cfg_.main_file || pm.m.id != cfg_.id) {
        log(B3MOD_LOG_ERROR, "the script did not load: its mod.ini changed after it was read (start the game again)");
        snap_.clear();
        return false;
    }
    cfg_.gameplay = pm.m.script.gameplay;
    cfg_.unsafe = pm.m.script.unsafe;
    cfg_.calls = pm.m.script.calls;
    snapped_ = true;
    return true;
}

std::uint64_t ScriptMod::signature() const {
    // the script folder's files: names, sizes, modification times (hot reload looks for a change)
    std::error_code ec;
    const fs::path base = cfg_.dir / b3mod::path_from_utf8(folder_of(cfg_.main_file));
    std::vector<std::string> rows;
    for (fs::recursive_directory_iterator it(base, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code e2;
        if (!it->is_regular_file(e2)) continue;
        const auto mt = fs::last_write_time(it->path(), e2).time_since_epoch().count();
        rows.push_back(b3mod::path_utf8(it->path()) + " " + std::to_string(it->file_size(e2)) + " " + std::to_string(static_cast<long long>(mt)));
    }
    std::sort(rows.begin(), rows.end());
    std::string t;
    for (const std::string& r : rows) t += r + "\n";
    return b3mod::fnv1a64(t.data(), t.size());
}

void ScriptMod::reload() {
    log(B3MOD_LOG_INFO, "its script changed: loaded again at VBlank " + std::to_string(vblank()) +
                            " (hot reload, Developer mods: from here this run does not play back as recorded)");
    for (const auto& h : hooks_) h->bound = false;
    last_state_ = last_laps_ = -1;
    std::string text;
    if (!snapshot(false) || !read_script(*this, text)) {   // (Developer mods: the edited files run unasked)
        close();
        dead_ = true;
        return;
    }
    CodeMods::release_textures(self_);   // modsec: the new state makes its own
    if (!open_state()) return;
    reloading_ = true;
    used_ = 0;
    time_used_ = 0;
    limit_ = cfg_.budget * kScriptLoadBudgetFactor;
    run_main(text);
    reloading_ = false;
    used_ = 0;
    time_used_ = 0;
    limit_ = cfg_.budget;
}

int ScriptMod::main(const B3ModApi* api, B3ModSelf* self) {
    api_ = api;
    self_ = self;
    in_main_ = true;
    used_ = 0;
    time_used_ = 0;
    limit_ = cfg_.budget * kScriptLoadBudgetFactor;
    std::string text;
    const bool ok = snapshot(true) && read_script(*this, text) && open_state() && run_main(text);
    in_main_ = false;
    if (!ok) return 1;
    api_->on_vblank(self_, [](void* u) { static_cast<ScriptMod*>(u)->on_vblank(); }, this);
    api_->on_frame(self_, [](void* u) { static_cast<ScriptMod*>(u)->on_frame(); }, this);
    api_->on_shutdown(self_, [](void* u) { static_cast<ScriptMod*>(u)->on_shutdown(); }, this);
    used_ = 0;
    time_used_ = 0;
    limit_ = cfg_.budget;
    if (cfg_.developer) sig_ = signature();
    return 0;
}

// ================================================================================================ the callbacks
void ScriptMod::on_vblank() {
    const std::uint64_t vb = vblank();
    if (cfg_.developer && vb >= next_look_) {   // hot reload: a look at the script folder every reload_every VBlanks
        next_look_ = vb + std::max(1u, cfg_.reload_every);
        const std::uint64_t s = signature();
        if (s != sig_) {
            sig_ = s;
            reload();
        }
    }
    close_if_dead();
    if (dead_ || !L_) return;
    time_used_ = 0;
    used_ = 0;   // this VBlank's budget (the frames and the hooks until the next VBlank count to it)
    limit_ = cfg_.budget;
    // the events read from the player's score: race_start (its state becomes 2, racing), lap (laps done grows)
    if (wants(kRaceStart) || wants(kLap)) {
        if (!in_race()) {
            last_state_ = last_laps_ = -1;
        } else {
            const int state = std::int32_t(u32(sa::kPlayerScore + 0x27C));
            const int laps = std::int32_t(u32(sa::kPlayerScore + 0x18));
            if (wants(kRaceStart) && state == 2 && last_state_ != 2 && last_state_ != -1)
                run_list(event_list(kRaceStart), [&](lua_State* L) {
                    lua_createtable(L, 0, 1);
                    set_int(L, "vblank", lua_Integer(vb));
                });
            if (wants(kLap) && !dead_ && last_laps_ >= 0 && laps > last_laps_ && laps <= 8) {
                const float t = f32(sa::kPlayerScore + 0x1C + 4 * std::uint32_t(laps - 1));
                run_list(event_list(kLap), [&](lua_State* L) {
                    lua_createtable(L, 0, 2);
                    set_int(L, "lap", laps);
                    set_num(L, "time", t);
                });
            }
            last_state_ = state;
            last_laps_ = laps;
        }
    }
    if (!dead_) run_list("vblank", [&](lua_State* L) { lua_pushinteger(L, lua_Integer(vb)); });
    close_if_dead();
}

void ScriptMod::on_frame() {
    close_if_dead();
    if (dead_ || !L_) return;
    run_list("frame", [](lua_State*) {});
    close_if_dead();
}

void ScriptMod::on_shutdown() {
    if (!dead_ && L_) {
        used_ = 0;
    time_used_ = 0;
        limit_ = cfg_.budget;
        run_list("shutdown", [](lua_State*) {});
    }
    dead_ = true;
    close_if_dead();
}

}  // namespace

std::shared_ptr<CodeMods::Builtin> make_script_mod(const ScriptConfig& c) { return std::make_shared<ScriptMod>(c); }

}  // namespace b3native

// script_lua.cpp's pattern matcher, at each step: true once the running script is past its time limit (the matcher then
// raises "pattern too complex" in that script's state; the count hook stops the script)
bool b3_script_c_tick() {
    b3native::ScriptMod* m = b3native::g_running;
    if (!m) return false;
    if (m->timed_out_) return true;
    return (++m->ticks_ & 1023u) == 0 && m->past_time();
}

namespace b3native {

CodeMods::Candidate script_candidate(const b3mod::ActiveMod& m, bool developer, const std::string& trust_digest) {
    ScriptConfig c;
    c.id = m.id;
    c.version = m.version;
    c.dir = m.dir;
    c.main_file = m.script.main_file;
    c.gameplay = m.script.gameplay;
    c.unsafe = m.script.unsafe;
    c.calls = m.script.calls;
    c.trust_digest = trust_digest;
    c.budget = m.script.budget;
    c.memory = std::uint64_t(m.script.memory_mb) << 20;
    c.developer = developer;
    CodeMods::Candidate k;
    k.id = m.id;
    k.version = m.version;
    k.dir = m.dir;
    k.builtin = make_script_mod(c);
    return k;
}

}  // namespace b3native
