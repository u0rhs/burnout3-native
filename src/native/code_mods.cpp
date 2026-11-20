// The code-mod loader and the B3ModApi (src/native/code_mods.h, sdk/include/b3mod_api.h).
#include "code_mods.h"

#include "../../sdk/include/b3mod_api.h"

#include "b3host/call.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

// One running hook (B3ModApi.next)
struct B3ModHookCtx {
    b3native::HookEntry next;
    unsigned n_gpr, n_fpr;
};

// A mod as the loader knows it (opaque to the mod)
struct B3ModSelf {
    b3native::CodeMods* owner = nullptr;
    std::string id, version, dir_utf8;
    std::filesystem::path dir;
    bool registering = false;
    std::vector<std::string> refusals;   // registration calls refused in b3mod_main (the first is the reason)
    struct HookRec {
        B3ModHookFn fn;
        void* user;
    };
    std::vector<std::unique_ptr<HookRec>> recs;
    std::vector<b3native::Hook> hooks;
    unsigned replaces = 0, wraps = 0;
    std::vector<std::pair<B3ModEventFn, void*>> on_vblank, on_frame, on_shutdown;
    std::vector<b3mod::SettingRow> rows;   // added in b3mod_main (into the live settings once loaded)
    std::vector<std::pair<std::uint32_t, std::vector<std::uint8_t>>> deferred;   // writes made in b3mod_main
    std::vector<b3mod::OverlayItem> items;   // queued since the last frame
    bool items_warned = false, late_warned = false, textures_warned = false;
    std::vector<std::uint32_t> textures;   // modsec: its live texture ids, and their bytes (CodeMods::kModTexture*)
    std::uint64_t texture_bytes = 0;
    void* lib = nullptr;
    std::shared_ptr<b3native::CodeMods::Builtin> builtin;   // lua6: a script mod's runtime (kept as long as the mod)
};

namespace b3native {

namespace {

std::string hex(std::uint32_t a) {
    char b[16];
    std::snprintf(b, sizeof b, "0x%08X", a);
    return b;
}

CodeMods* g_current = nullptr;   // the process's loader (native_play makes one)
std::string g_port_version;
std::uint32_t g_texture_ids = 0;

// ---- the library ----
void* open_library(const std::filesystem::path& p, std::string& why) {
#ifdef _WIN32
    const std::wstring w = std::filesystem::absolute(p).wstring();
    HMODULE m = LoadLibraryExW(w.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!m) why = "the library did not load (Windows error " + std::to_string(GetLastError()) + ")";
    return reinterpret_cast<void*>(m);
#else
    void* h = dlopen(std::filesystem::absolute(p).c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!h) {
        const char* e = dlerror();
        why = std::string("the library did not load (") + (e ? e : "?") + ")";
    }
    return h;
#endif
}
void* library_symbol(void* lib, const char* name) {
#ifdef _WIN32
    return reinterpret_cast<void*>(GetProcAddress(reinterpret_cast<HMODULE>(lib), name));
#else
    return dlsym(lib, name);
#endif
}
void close_library(void* lib) {
    if (!lib) return;
#ifdef _WIN32
    FreeLibrary(reinterpret_cast<HMODULE>(lib));
#else
    dlclose(lib);
#endif
}

// The files a library load reads, held open without write or delete sharing (Windows) from their last hash to the
// load, so what loads is what was hashed. Elsewhere a no-op (the loader hashes right before dlopen; a short window
// stays: dlopen of /proc/self/fd/<n> would close it, but breaks $ORIGIN for the libraries beside the mod's, so it is
// documented instead, sdk-reference.md "Trust").
class FileHolds {
public:
    FileHolds() = default;
    FileHolds(const FileHolds&) = delete;
    FileHolds& operator=(const FileHolds&) = delete;
#ifdef _WIN32
    ~FileHolds() {
        for (HANDLE h : held_) CloseHandle(h);
    }
    void hold(const std::filesystem::path& p) {
        const HANDLE h = CreateFileW(std::filesystem::absolute(p).wstring().c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                     FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h != INVALID_HANDLE_VALUE) held_.push_back(h);
    }

private:
    std::vector<HANDLE> held_;
#else
    void hold(const std::filesystem::path&) {}
#endif
};

// ---- registers ----
void to_regs(const b3host::CallArgs& a, B3ModRegs& r) {
    std::memset(&r, 0, sizeof r);
    for (unsigned i = 0; i < 8; ++i) {
        r.gpr[i] = std::uint64_t(a.gpr[i].w[0]) | std::uint64_t(a.gpr[i].w[1]) << 32;
        r.gpr_hi[i] = std::uint64_t(a.gpr[i].w[2]) | std::uint64_t(a.gpr[i].w[3]) << 32;
        r.fpr[i] = a.fpr[i];
    }
    r.sp = a.sp;
}
void from_regs(const B3ModRegs& r, b3host::CallArgs& a, unsigned n_gpr, unsigned n_fpr) {
    for (unsigned i = 0; i < 8; ++i) {
        a.gpr[i].w[0] = std::uint32_t(r.gpr[i]);
        a.gpr[i].w[1] = std::uint32_t(r.gpr[i] >> 32);
        a.gpr[i].w[2] = std::uint32_t(r.gpr_hi[i]);
        a.gpr[i].w[3] = std::uint32_t(r.gpr_hi[i] >> 32);
        a.fpr[i] = r.fpr[i];
    }
    a.n_gpr = n_gpr;
    a.n_fpr = n_fpr;
    a.sp = r.sp;
}
void to_result(const b3host::CallResult& c, B3ModResult& r) {
    std::memset(&r, 0, sizeof r);
    r.v0 = std::uint64_t(c.v0.w[0]) | std::uint64_t(c.v0.w[1]) << 32;
    r.v0_hi = std::uint64_t(c.v0.w[2]) | std::uint64_t(c.v0.w[3]) << 32;
    r.v1 = c.v1;
    r.f0 = c.f0;
}
void from_result(const B3ModResult& r, b3host::CallResult& c) {
    c.v0.w[0] = std::uint32_t(r.v0);
    c.v0.w[1] = std::uint32_t(r.v0 >> 32);
    c.v0.w[2] = std::uint32_t(r.v0_hi);
    c.v0.w[3] = std::uint32_t(r.v0_hi >> 32);
    c.v1 = r.v1;
    c.f0 = r.f0;
}

// the bound hook: the mod's function with the layer below as its context
void hook_trampoline(const b3host::CallArgs& a, b3host::CallResult& r, void* user, HookEntry next) {
    const auto* h = static_cast<const B3ModSelf::HookRec*>(user);
    B3ModRegs regs;
    to_regs(a, regs);
    B3ModResult res;
    to_result(r, res);
    B3ModHookCtx ctx{next, a.n_gpr, a.n_fpr};
    h->fn(&ctx, &regs, &res, h->user);
    from_result(res, r);
}

}  // namespace

struct CodeMods::Impl {
    static CodeMods& of(B3ModSelf* s) { return *s->owner; }
    static void say(CodeMods& c, const std::string& line) {
        if (c.host_.log) c.host_.log(line);
        else {
            std::printf("%s\n", line.c_str());
            std::fflush(stdout);
        }
    }
    // a registration call: only inside b3mod_main; a refusal there fails the mod
    static bool registration(B3ModSelf* s, const char* what) {
        if (s->registering) return true;
        if (!s->late_warned) {
            s->late_warned = true;
            say(of(s), "mod " + s->id + ": " + what + " after b3mod_main returned: refused (registrations go in b3mod_main)");
        }
        return false;
    }
    static int refuse(B3ModSelf* s, const std::string& why) {
        s->refusals.push_back(why);
        return 0;
    }
    // guest bytes [addr, addr + n): in EE RAM or the scratchpad (and, for a write, outside the program's code)
    static bool guest_range(std::uint32_t addr, std::uint32_t n, bool write) {
        if (n == 0) return true;
        const b3host::Decoded d = b3host::decode(addr, n);
        if (d.region == b3host::Region::Other) return false;
        if (write && d.region == b3host::Region::MainRam && d.offset < kProgramCodeEnd && d.offset + n > kProgramCodeBegin) return false;
        return true;
    }

    // ---- the API ----
    static const char* port_version() { return g_port_version.c_str(); }
    static std::uint64_t vblank() { return g_current && g_current->host_.vblank ? g_current->host_.vblank() : 0; }
    static const char* mod_id(const B3ModSelf* s) { return s ? s->id.c_str() : ""; }
    static const char* mod_dir(const B3ModSelf* s) { return s ? s->dir_utf8.c_str() : ""; }
    static void log(B3ModSelf* s, int level, const char* text) {
        if (!s) return;
        say(of(s), "mod " + s->id + ": " + (level == B3MOD_LOG_WARN ? "warning: " : level == B3MOD_LOG_ERROR ? "error: " : "") + (text ? text : ""));
    }
    static int hook(B3ModSelf* s, std::uint32_t addr, B3ModHookFn fn, void* user, HookLayer layer) {
        const char* what = layer == HookLayer::kReplace ? "replace" : "wrap";
        if (!s || !registration(s, what)) return 0;
        CodeMods& c = of(s);
        if (!fn) return refuse(s, std::string(what) + " " + hex(addr) + ": no function");
        if (!c.table_.has_row(addr)) return refuse(s, std::string(what) + " " + hex(addr) + ": no native function there (not native, or inlined in the decomp)");
        for (const Hook& h : s->hooks)
            if (h.address == addr && h.layer == layer) return refuse(s, std::string(what) + " " + hex(addr) + " twice");
        s->recs.push_back(std::make_unique<B3ModSelf::HookRec>(B3ModSelf::HookRec{fn, user}));
        const Hook h = bind_hook(addr, layer, &hook_trampoline, s->recs.back().get());
        if (!h.fn) return refuse(s, std::string(what) + " " + hex(addr) + ": no hook slot left (" + std::to_string(kBoundSlots) + " in all)");
        s->hooks.push_back(h);
        ++(layer == HookLayer::kReplace ? s->replaces : s->wraps);
        return 1;
    }
    static int replace(B3ModSelf* s, std::uint32_t addr, B3ModHookFn fn, void* user) { return hook(s, addr, fn, user, HookLayer::kReplace); }
    static int wrap(B3ModSelf* s, std::uint32_t addr, B3ModHookFn fn, void* user) { return hook(s, addr, fn, user, HookLayer::kWrap); }
    static void next(B3ModHookCtx* ctx, const B3ModRegs* regs, B3ModResult* result) {
        if (!ctx || !regs || !ctx->next) return;
        b3host::CallArgs a;
        from_regs(*regs, a, ctx->n_gpr, ctx->n_fpr);
        b3host::CallResult r;
        ctx->next(a, r);
        if (result) to_result(r, *result);
    }
    static int call(B3ModSelf* s, std::uint32_t addr, const B3ModRegs* regs, B3ModResult* result) {
        if (!s || !of(s).table_.has_row(addr)) return 0;
        b3host::CallArgs a;
        B3ModRegs in{};
        if (regs) in = *regs;
        from_regs(in, a, 8, 8);
        if (a.sp == 0) a.sp = b3host::backend().stack_pointer();
        b3host::CallResult r;
        b3host::detail::dispatch(addr, a, r);
        if (result) to_result(r, *result);
        return 1;
    }
    static int has_function(B3ModSelf* s, std::uint32_t addr) { return s && of(s).table_.has_row(addr) ? 1 : 0; }
    static int read(B3ModSelf* s, std::uint32_t addr, void* dst, std::uint32_t n) {
        if (!s || (!dst && n) || !guest_range(addr, n, false)) return 0;
        if (n) b3host::copy_from_guest(dst, addr, n);
        return 1;
    }
    static int write(B3ModSelf* s, std::uint32_t addr, const void* src, std::uint32_t n) {
        if (!s || (!src && n) || !guest_range(addr, n, true)) {
            if (s && n && b3host::decode(addr, n).region != b3host::Region::Other)
                say(of(s), "mod " + s->id + ": write " + hex(addr) + " (" + std::to_string(n) +
                               " bytes) refused: the program's code never runs natively (change a function with a hook)");
            return 0;
        }
        if (!n) return 1;
        if (s->registering) {   // applied when the mod has loaded (all or nothing)
            const auto* p = static_cast<const std::uint8_t*>(src);
            s->deferred.emplace_back(addr, std::vector<std::uint8_t>(p, p + n));
            return 1;
        }
        b3host::copy_to_guest(addr, src, n);
        return 1;
    }
    static std::uint32_t alloc(B3ModSelf* s, std::uint32_t bytes, std::uint32_t align) {
        if (!s || !registration(s, "alloc")) return 0;
        std::string err;
        const b3host::gaddr a = of(s).arena_.alloc("mod:" + s->id, bytes, align, &err);
        if (!a) refuse(s, "alloc: " + err);
        return a;
    }
    static int add_setting(B3ModSelf* s, const B3ModSetting* st) {
        if (!s || !registration(s, "add_setting")) return 0;
        if (!st || st->size < sizeof(B3ModSetting)) return refuse(s, "add_setting: not a B3ModSetting (its size)");
        b3mod::SettingRow r;
        r.mod = s->id;
        r.key = st->key ? st->key : "";
        r.label = st->label ? st->label : "";
        r.description = st->description ? st->description : "";
        r.def = st->default_value;
        switch (st->kind) {
        case B3MOD_SETTING_TOGGLE:
            r.kind = b3mod::SettingKind::kToggle;
            r.count = 2;
            r.values = {"Off", "On"};
            if (st->count != 0 && st->count != 2) return refuse(s, "add_setting " + r.key + ": a toggle has two values");
            break;
        case B3MOD_SETTING_CHOICE:
            r.kind = b3mod::SettingKind::kChoice;
            r.count = st->count;
            if (!st->values || st->count > 16) return refuse(s, "add_setting " + r.key + ": a choice needs its value names (2 to 16)");
            for (std::uint32_t k = 0; k < st->count; ++k) r.values.push_back(st->values[k] ? st->values[k] : "");
            break;
        case B3MOD_SETTING_SLIDER:
            r.kind = b3mod::SettingKind::kSlider;
            r.count = st->count;
            break;
        default: return refuse(s, "add_setting " + r.key + ": kind " + std::to_string(st->kind) + " is not a toggle, a choice or a slider");
        }
        if (const std::string why = b3mod::check_row(r); !why.empty()) return refuse(s, "add_setting " + why);
        for (const b3mod::SettingRow& o : s->rows)
            if (o.key == r.key) return refuse(s, "add_setting " + r.key + ": added twice");
        r.value = int(r.def);
        CodeMods& c = of(s);
        if (c.host_.saved_values) {
            const auto saved = c.host_.saved_values(s->id);
            if (const auto it = saved.find(r.key); it != saved.end()) {
                const int v = b3mod::value_from_text(r, it->second);
                if (v >= 0) r.value = v;
                else say(c, "mod " + s->id + ": setting " + r.key + " = \"" + it->second + "\" is not one of its values: " + b3mod::value_text(r, r.value));
            }
        }
        s->rows.push_back(std::move(r));
        return 1;
    }
    static int get_setting(B3ModSelf* s, const char* key) {
        if (!s || !key) return -1;
        CodeMods& c = of(s);
        const int i = c.settings_.find(s->id, key);
        if (i >= 0) return c.settings_.value(i);
        for (const b3mod::SettingRow& r : s->rows)   // (inside b3mod_main: the rows not in the live settings yet)
            if (r.key == key) return r.value;
        return -1;
    }
    static std::uint32_t texture(B3ModSelf* s, std::uint32_t w, std::uint32_t h, const std::uint8_t* rgba) {
        if (!s || !rgba || w == 0 || h == 0 || w > 1024 || h > 1024) return 0;
        const std::uint64_t bytes = std::uint64_t(w) * h * 4;
        if (s->textures.size() >= kModTextures || s->texture_bytes + bytes > kModTextureBytes) {
            if (!s->textures_warned) {
                s->textures_warned = true;
                say(of(s), "mod " + s->id + ": texture refused: a mod may have " + std::to_string(kModTextures) + " textures of " +
                               std::to_string(kModTextureBytes >> 20) + " MB in all (" + std::to_string(s->textures.size()) + " made, " +
                               std::to_string(s->texture_bytes >> 10) + " KB): make each once, at the start");
            }
            return 0;
        }
        const std::uint32_t id = ++g_texture_ids;
        CodeMods& c = of(s);
        if (c.host_.texture) c.host_.texture(id, w, h, rgba);
        s->textures.push_back(id);
        s->texture_bytes += bytes;
        return id;
    }
    static bool room(B3ModSelf* s) {
        if (s->items.size() < b3mod::kOverlayItemsPerMod) return true;
        if (!s->items_warned) {
            s->items_warned = true;
            say(of(s), "mod " + s->id + ": more than " + std::to_string(b3mod::kOverlayItemsPerMod) + " overlay items in a frame: the rest are not drawn");
        }
        return false;
    }
    static void quad(B3ModSelf* s, float x, float y, float w, float h, std::uint32_t rgba, std::uint32_t tex) {
        if (!s || !room(s)) return;
        b3mod::OverlayItem it;
        it.kind = 1;
        it.rgba = rgba;
        it.x = x;
        it.y = y;
        it.w = w;
        it.h = h;
        it.texture = tex;
        s->items.push_back(std::move(it));
    }
    static void text(B3ModSelf* s, float x, float y, float size, std::uint32_t rgba, const char* utf8) {
        if (!s || !utf8 || !room(s)) return;
        b3mod::OverlayItem it;
        it.kind = 2;
        it.rgba = rgba;
        it.x = x;
        it.y = y;
        it.h = size;
        it.text = std::string(utf8).substr(0, 256);
        s->items.push_back(std::move(it));
    }
    static int event(B3ModSelf* s, B3ModEventFn fn, void* user, std::vector<std::pair<B3ModEventFn, void*>> B3ModSelf::*list, const char* what) {
        if (!s || !registration(s, what)) return 0;
        if (!fn) return refuse(s, std::string(what) + ": no function");
        (s->*list).emplace_back(fn, user);
        return 1;
    }
    static int on_vblank(B3ModSelf* s, B3ModEventFn fn, void* user) { return event(s, fn, user, &B3ModSelf::on_vblank, "on_vblank"); }
    static int on_frame(B3ModSelf* s, B3ModEventFn fn, void* user) { return event(s, fn, user, &B3ModSelf::on_frame, "on_frame"); }
    static int on_shutdown(B3ModSelf* s, B3ModEventFn fn, void* user) { return event(s, fn, user, &B3ModSelf::on_shutdown, "on_shutdown"); }
    // "disc:<path>" or a path in the mod's folder
    static bool mod_file(B3ModSelf* s, const char* path, std::filesystem::path& out) {
        if (!path || !b3mod::safe_relative(path)) return false;
        out = s->dir / b3mod::path_from_utf8(path);
        return true;
    }
    static std::int64_t data_size(B3ModSelf* s, const char* path) {
        if (!s || !path) return -1;
        CodeMods& c = of(s);
        if (std::strncmp(path, "disc:", 5) == 0) return c.host_.disc_size ? c.host_.disc_size(path + 5) : -1;
        std::filesystem::path p;
        std::error_code ec;
        if (!mod_file(s, path, p) || !std::filesystem::is_regular_file(p, ec)) return -1;
        return static_cast<std::int64_t>(std::filesystem::file_size(p, ec));
    }
    static std::int64_t read_data(B3ModSelf* s, const char* path, std::uint64_t offset, void* dst, std::uint64_t n) {
        if (!s || !path || (!dst && n)) return -1;
        CodeMods& c = of(s);
        if (std::strncmp(path, "disc:", 5) == 0) return c.host_.disc_read ? c.host_.disc_read(path + 5, offset, dst, n) : -1;
        std::filesystem::path p;
        if (!mod_file(s, path, p)) return -1;
        std::ifstream f(p, std::ios::binary);
        if (!f) return -1;
        f.seekg(static_cast<std::streamoff>(offset));
        if (!f) return 0;
        f.read(static_cast<char*>(dst), static_cast<std::streamsize>(n));
        return static_cast<std::int64_t>(f.gcount());
    }

    static const B3ModApi& api() {
        static const B3ModApi a = [] {
            B3ModApi x{};
            x.size = sizeof(B3ModApi);
            x.version = B3MOD_API_VERSION;
            x.port_version = &port_version;
            x.vblank = &vblank;
            x.mod_id = &mod_id;
            x.mod_dir = &mod_dir;
            x.log = &log;
            x.replace = &replace;
            x.wrap = &wrap;
            x.next = &next;
            x.call = &call;
            x.has_function = &has_function;
            x.read = &read;
            x.write = &write;
            x.alloc = &alloc;
            x.add_setting = &add_setting;
            x.get_setting = &get_setting;
            x.texture = &texture;
            x.quad = &quad;
            x.text = &text;
            x.on_vblank = &on_vblank;
            x.on_frame = &on_frame;
            x.on_shutdown = &on_shutdown;
            x.data_size = &data_size;
            x.read_data = &read_data;
            return x;
        }();
        return a;
    }
};

CodeMods::CodeMods(HookTable& table, GuestArena& arena, Host host) : table_(table), arena_(arena), host_(std::move(host)) {
    g_current = this;
    g_port_version = host_.port_version;
}

CodeMods::~CodeMods() {
    if (g_current == this) g_current = nullptr;
    // the libraries stay loaded: the hook table keeps their functions for the rest of the process
}

void CodeMods::release_textures(B3ModSelf* s) {
    if (!s || !s->owner) return;
    CodeMods& c = *s->owner;
    if (c.host_.texture_free)
        for (const std::uint32_t id : s->textures) c.host_.texture_free(id);
    s->textures.clear();
    s->texture_bytes = 0;
    s->textures_warned = false;
}

std::vector<std::string> CodeMods::loaded_ids() const {
    std::vector<std::string> v;
    for (const auto& s : loaded_) v.push_back(s->id);
    return v;
}

std::vector<CodeMods::Result> CodeMods::load(const std::vector<Candidate>& mods, bool developer) {
    std::vector<Result> out;
    for (const Candidate& m : mods) {
        Result res;
        res.id = m.id;
        auto fail = [&](const std::string& why, B3ModSelf* s) {
            res.reason = why;
            Impl::say(*this, "native_play: mods: " + m.id + ": not loaded: " + why);
            if (s) release_textures(s);   // (made in its b3mod_main)
            if (s) close_library(s->lib);
            out.push_back(res);
        };
        if (m.library.empty() && !m.builtin) { fail(std::string("[code] names no library for ") + b3mod::host_system(), nullptr); continue; }
        auto s = std::make_unique<B3ModSelf>();
        s->owner = this;
        s->id = m.id;
        s->version = m.version;
        s->dir = m.dir;
        s->dir_utf8 = b3mod::path_utf8(std::filesystem::absolute(m.dir));
        using Main = int (*)(const B3ModApi*, B3ModSelf*);
        Main main_fn = nullptr;
        if (m.builtin) {
            s->builtin = m.builtin;
        } else {
            std::string why;
            FileHolds holds;   // (Windows: no writer or delete from this hash to the load)
            if (!m.trust_digest.empty()) {
                std::vector<b3mod::TrustFile> files;
                if (!b3mod::trust_files(m.dir, m.code, b3mod::ScriptSpec{}, files, &why)) { fail("its libraries cannot be checked: " + why, nullptr); continue; }
                for (const b3mod::TrustFile& f : files) holds.hold(m.dir / b3mod::path_from_utf8(f.rel));
                files.clear();
                if (!b3mod::trust_files(m.dir, m.code, b3mod::ScriptSpec{}, files, &why) || b3mod::trust_digest(files) != m.trust_digest) {
                    fail("its libraries changed after they were checked (" + (why.empty() ? std::string("a file differs") : why) + "): start the game again", nullptr);
                    continue;
                }
            }
            s->lib = open_library(m.library, why);
            if (!s->lib) { fail(why + ": " + b3mod::path_utf8(m.library), nullptr); continue; }
            using BuiltFor = unsigned (*)();
            main_fn = reinterpret_cast<Main>(library_symbol(s->lib, "b3mod_main"));
            const auto built_for = reinterpret_cast<BuiltFor>(library_symbol(s->lib, "b3mod_built_for"));
            if (!main_fn) { fail("the library has no b3mod_main", s.get()); continue; }
            if (!built_for) { fail("the library has no b3mod_built_for (B3MOD_DECLARE_VERSION() in one of its sources)", s.get()); continue; }
            const unsigned built = built_for();
            if (built == 0 || built > B3MOD_API_VERSION) {
                fail("built for SDK API version " + std::to_string(built) + "; this port has version " + std::to_string(B3MOD_API_VERSION) +
                         (built > B3MOD_API_VERSION ? ": a newer port is needed" : ""),
                     s.get());
                continue;
            }
        }
        const std::size_t arena_mark = arena_.allocations().size();
        s->registering = true;
        const int rc = s->builtin ? s->builtin->main(&Impl::api(), s.get()) : main_fn(&Impl::api(), s.get());
        s->registering = false;
        std::string err;
        if (rc != 0 && s->builtin) err = s->refusals.empty() ? std::string("its ") + s->builtin->kind() + " did not load (the lines above say why)" : s->refusals.front();
        else if (rc != 0) err = "b3mod_main returned " + std::to_string(rc) + (s->refusals.empty() ? "" : " (" + s->refusals.front() + ")");
        else if (!s->refusals.empty()) err = s->refusals.front();
        else if (!s->hooks.empty() && !table_.install("mod:" + m.id, s->hooks, &err, developer && m.platform)) {
            // the table's own words ("mod:b conflicts with mod:a: both replace ..."), without the prefixes
            for (std::size_t at; (at = err.find("mod:")) != std::string::npos;) err.erase(at, 4);
            if (err.rfind(m.id + ": ", 0) == 0) err.erase(0, m.id.size() + 2);
        }
        if (!err.empty()) {
            arena_.truncate(arena_mark);
            fail(err, s.get());
            continue;
        }
        for (const auto& [addr, bytes] : s->deferred) b3host::copy_to_guest(addr, bytes.data(), std::uint32_t(bytes.size()));
        s->deferred.clear();
        for (const b3mod::SettingRow& r : s->rows) settings_.add(r);
        res.loaded = true;
        res.hooks = unsigned(s->hooks.size());
        res.rows = unsigned(s->rows.size());
        Impl::say(*this, "native_play: mods: " + m.id + ": " + (s->builtin ? s->builtin->kind() : "code") + " loaded (" + std::to_string(s->replaces) + " replaced, " + std::to_string(s->wraps) +
                             " wrapped, " + std::to_string(s->rows.size()) + " settings, " + std::to_string(s->on_vblank.size() + s->on_frame.size()) +
                             " callbacks)");
        loaded_.push_back(std::move(s));
        out.push_back(res);
    }
    return out;
}

void CodeMods::vblank() {
    for (const auto& s : loaded_)
        for (const auto& [fn, user] : s->on_vblank) fn(user);
}

void CodeMods::frame() {
    for (const auto& s : loaded_)
        for (const auto& [fn, user] : s->on_frame) fn(user);
}

std::vector<std::uint8_t> CodeMods::overlay_record() {
    std::vector<b3mod::OverlayItem> all;   // the first mod in the load order on top: drawn last
    for (auto it = loaded_.rbegin(); it != loaded_.rend(); ++it) {
        all.insert(all.end(), (*it)->items.begin(), (*it)->items.end());
        (*it)->items.clear();
    }
    if (overlay_published_ && all == last_overlay_) return {};
    if (!overlay_published_ && all.empty()) return {};
    overlay_published_ = true;
    last_overlay_ = all;
    return b3mod::overlay_record(++overlay_serial_, all);
}

void CodeMods::shutdown() {
    if (shut_) return;
    shut_ = true;
    for (auto it = loaded_.rbegin(); it != loaded_.rend(); ++it)
        for (const auto& [fn, user] : (*it)->on_shutdown) fn(user);
}

}  // namespace b3native
