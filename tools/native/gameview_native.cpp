// gameview_native.cpp -- the game view on the native backend (pic1; header gameview_native.h, docs/native-renderer.md §32).
#include "gameview_native.h"

#include "b3gameview/gameview.h"
#include "b3host/native_backend.h"
#include "b3live/menu_input.h"
#include "b3mod/codemod.h"   // sdk2b: the code mods' overlay record

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>
#include <vector>

namespace {

constexpr b3host::gaddr kUpdatePort = 0x00214610u;   // [C] UpdatePort__22CGtInputManagerPS2Pad2F15EGtPS2InputPort
// mac5: [C] PresentDrawBuffer__18CB3GraphicsManagerFv (the main loop's last call, func_00132600 0x132FF4). A frame that
// ends without a pad poll gets its poll here: the game reads no pad while it loads (the post-race loading page, Mac
// 2026-11-08: no poll for 167 VBlanks, the renderer kept the page's first frame -- LOADING at alpha 0.04 -- throughout)
constexpr b3host::gaddr kPresent = 0x002270B0u;
bool g_polled = false;   // a poll since the last present

bool g_on = false;
std::uint64_t (*g_vblank)() = nullptr;
std::uint64_t g_poll = 0;
std::vector<std::uint64_t> g_bits;   // b3host::g_watch_bits
struct Extra { b3host::gaddr address = 0; void (*fn)(std::uint32_t, const std::uint32_t*) = nullptr; };
Extra g_extra[4];   // mac4: watch_extra

b3gv::Memory arena() {
    auto& nb = static_cast<b3host::NativeBackend&>(b3host::backend());
    b3gv::Memory m;
    m.ram = nb.ram().data();
    m.ram_size = std::uint32_t(nb.ram().size() < b3gv::kRamEnd ? nb.ram().size() : b3gv::kRamEnd);
    m.spr = nb.scratchpad().size() >= b3gv::kSprSize ? nb.scratchpad().data() : nullptr;
    return m;
}

// The schedule stamp: the host time the poll's input was latched, which natively is now (the poll is the latch).
// fastload1: none (0) while a load runs unpaced (set_unstamped): its ticks are not on the 1/60 s clock, so the
// renderer's schedule clock (src/b3replay/clock.h step_sched: the rate over the last 30 stamps) starts afresh after it.
bool g_unstamped = false;
std::uint64_t stamp(std::uint64_t) {
    if (g_unstamped) return 0;
    return std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}

// b3host's watch hook: entries of the HUD's functions (before: result null) and UpdatePort's return.
void watch(b3host::gaddr f, const b3host::CallArgs& a, const b3host::CallResult* r) {
    if (f == kUpdatePort) {
        // play1: UpdatePort is a member (the guest-ABI wrapper b3gen_00214610(self, port)): $a0 is `this`, the port is
        // $a1 -- testing $a0 never matched natively (0 polls)
        if (r != nullptr && a.gpr[1].w[0] == 0) b3gv_native::poll();
        return;
    }
    if (f == kPresent) {
        if (r != nullptr) {
            if (!g_polled) b3gv_native::poll();
            g_polled = false;
        }
        return;
    }
    if (f == b3gv::kWorldRender) {   // split2: view 1's per-vehicle results before Render(1) overwrites them
        if (r == nullptr) b3gv::on_world_render(int(a.gpr[1].w[0]), arena());
        return;
    }
    if (r != nullptr) return;
    std::uint32_t arg[8];
    for (unsigned i = 0; i < 8; ++i) arg[i] = a.gpr[i].w[0];
    for (const Extra& x : g_extra)
        if (x.address == f && x.fn) { x.fn(f, arg); return; }
    b3gv::on_entry(f, arg, arena());
}

// mac5: the game's three 2D fonts for the renderer's PC SETTINGS page (src/b3replay/hud.h kHostFontBase, 'HFN1'):
// published when all three are prepared (CGt2dFont +0x18 = 1 [C Prepare 0x1D8210]) and again if a pointer changes.
// Reads only.
constexpr b3host::gaddr kGraphicsManager = 0x00665EC0u, kHostFontBase = 0x41102000u, kHostFontMax = 0x10000u;
std::uint32_t g_font_key[3] = {0, 0, 0}, g_font_serial = 0;
void publish_fonts() {
    std::uint32_t font[3];
    for (unsigned k = 0; k < 3; ++k) {
        font[k] = b3host::load<std::uint32_t>(kGraphicsManager + 0x10u + 4u * k) & 0x01FFFFFFu;
        if (font[k] == 0 || b3host::load<std::uint32_t>(font[k] + 0x18u) != 1u) return;   // not prepared yet
    }
    if (font[0] == g_font_key[0] && font[1] == g_font_key[1] && font[2] == g_font_key[2]) return;
    std::vector<std::uint8_t> out(16);
    const auto put32 = [&](std::uint32_t v) { for (int b = 0; b < 4; ++b) out.push_back(std::uint8_t(v >> (8 * b))); };
    for (unsigned k = 0; k < 3; ++k) {
        const b3host::gaddr f = font[k];
        // the glyph array: from the lowest bucket pointer to the end pointer (+0x1C, the last glyph) [C FindGlyph]
        b3host::gaddr lo = 0xFFFFFFFFu;
        for (unsigned i = 0; i < 0x80u; ++i) lo = std::min(lo, b3host::load<std::uint32_t>(f + 0x20u + 4u * i) & 0x01FFFFFFu);
        const b3host::gaddr hi = b3host::load<std::uint32_t>(f + 0x1Cu) & 0x01FFFFFFu;
        if (hi < lo || (hi - lo) % 0x20u != 0 || hi - lo > 0x20u * 2048u) return;
        const std::uint32_t n = (hi - lo) / 0x20u + 1u;
        put32(b3host::load<std::uint32_t>(f + 0x4u) & 0x01FFFFFFu);   // the texture (CGtTexture*)
        put32(b3host::load<std::uint32_t>(f + 0x8u));                  // cell scale x, y
        put32(b3host::load<std::uint32_t>(f + 0xCu));
        put32(b3host::load<std::uint32_t>(f + 0x10u));                 // baseline
        put32(n);
        for (b3host::gaddr g = lo; g < hi + 0x20u; g += 4) put32(b3host::load<std::uint32_t>(g));   // through the last glyph's end
        if (std::getenv("B3NATIVE_FONT_CODES")) {   // diagnostics: the font's codes past ASCII (button glyphs, accents)
            std::printf("gameview: font %u codes:", k);
            for (b3host::gaddr g = lo; g <= hi; g += 0x20u)
                if (const std::uint16_t c = b3host::load<std::uint16_t>(g + 0x1Cu); c > 0x7Eu) std::printf(" %04X", c);
            std::printf("\n");
        }
    }
    if (out.size() > kHostFontMax) return;
    ++g_font_serial;
    const std::uint32_t head[4] = {0x314E4648u, g_font_serial, std::uint32_t(out.size() - 16), 3u};   // 'HFN1'
    std::memcpy(out.data(), head, 16);
    b3scene_grab_host(kHostFontBase, out.data(), std::uint32_t(out.size()));
    for (unsigned k = 0; k < 3; ++k) g_font_key[k] = font[k];
    std::printf("gameview: the game's fonts published (%08X %08X %08X, %zu bytes) for the PC SETTINGS page\n", font[0], font[1],
                font[2], out.size());
}

// mac5: the video skip hint's opacity for the renderer (src/b3replay/hud.h kHostSkipBase, 'HSK1': {u32 'HSK1', u32 serial,
// u32 opacity 0..255, 0}), set per VBlank by native_play (tools/native/movie_skip.h SkipHintFade), published at the poll
// when it changed. Host bytes only.
constexpr std::uint32_t kHostSkipBase = 0x41112000u;
std::uint32_t g_skip_level = 0, g_skip_published = 0, g_skip_serial = 0;   // (nothing published before the first hint)
void publish_skip_hint() {
    if (g_skip_level == g_skip_published) return;
    g_skip_published = g_skip_level;
    const std::uint32_t r[4] = {0x314B5348u, ++g_skip_serial, g_skip_level, 0u};   // 'HSK1'
    b3scene_grab_host(kHostSkipBase, reinterpret_cast<const std::uint8_t*>(r), 16);
}

// mac5: the menus on screen for the renderer's mouse (include/b3live/menu_input.h 'HMN1' at kHostMenuBase), set by
// native_play (tools/native/menu_mouse.h) when they change, published at the next poll. Host bytes only.
std::vector<std::uint8_t> g_menu_record;
bool g_menu_dirty = false;
void publish_menu() {
    if (!g_menu_dirty || g_menu_record.empty() || g_menu_record.size() > b3live::kHostMenuMax) return;
    g_menu_dirty = false;
    b3scene_grab_host(b3live::kHostMenuBase, g_menu_record.data(), std::uint32_t(g_menu_record.size()));
}

// sdk2b: the code mods' overlay (include/b3mod/codemod.h 'HMO1' at kOverlayBase), set by native_play's frame hook
// (src/native/code_mods.h) when it changed, published at this poll. Host bytes only; nothing without code mods.
std::vector<std::uint8_t> g_mod_overlay;
bool g_mod_overlay_dirty = false;
void (*g_frame_hook)() = nullptr;
void publish_mod_overlay() {
    if (!g_mod_overlay_dirty || g_mod_overlay.empty() || g_mod_overlay.size() > b3mod::kOverlayMax) return;
    g_mod_overlay_dirty = false;
    b3scene_grab_host(b3mod::kOverlayBase, g_mod_overlay.data(), std::uint32_t(g_mod_overlay.size()));
}

// autosave1: the display's hold (src/b3replay/hud.h kHostHoldBase, 'HHD1': {u32 'HHD1', u32 serial, u32 hold, 0}), asked of
// native_play's probe at each poll, published when it changed. Host bytes only.
constexpr std::uint32_t kHostHoldBase = 0x41112100u;
bool (*g_hold_probe)() = nullptr;
std::uint32_t g_hold_published = 0, g_hold_serial = 0;
void publish_hold() {
    const std::uint32_t want = g_hold_probe && g_hold_probe() ? 1u : 0u;
    if (want == g_hold_published) return;
    g_hold_published = want;
    const std::uint32_t r[4] = {0x31444848u, ++g_hold_serial, want, 0u};   // 'HHD1'
    b3scene_grab_host(kHostHoldBase, reinterpret_cast<const std::uint8_t*>(r), 16);
}

void publish_host() {   // the poll's host records (b3gv::Host::extra_publish)
    publish_fonts();
    publish_skip_hint();
    publish_hold();
    publish_menu();
    publish_mod_overlay();
}

void set_env(const char* name, const std::string& value) {   // mingw's C runtime has no setenv
#ifdef _WIN32
    _putenv_s(name, value.c_str());
#else
    setenv(name, value.c_str(), 1);
#endif
}

void set_bit(b3host::gaddr a) {
    if (a < b3host::kWatchLo || a >= b3host::kWatchHi || (a & 3u)) return;
    const std::uint32_t i = (a - b3host::kWatchLo) >> 2;
    g_bits[i >> 6] |= std::uint64_t(1) << (i & 63);
}

}  // namespace

bool b3gv_native::install(const Options& o, const std::string& exe_dir) {
    if (o.mode != "offscreen" && o.mode != "window") {
        std::fprintf(stderr, "--render: offscreen or window, not '%s'\n", o.mode.c_str());
        return false;
    }
    // The renderer library (an explicit B3PORT_RENDER wins) and its arguments (an explicit B3PORT_RENDER_ARGS wins). An
    // offscreen run always gets --offscreen-live, whatever library or arguments it was given: it never opens a window.
    if (!std::getenv("B3PORT_RENDER")) {
#ifdef _WIN32
        const std::string lib = o.lib.empty() ? exe_dir + "/b3render.dll" : o.lib;
#else
        const std::string lib = o.lib.empty() ? exe_dir + "/libb3render.so" : o.lib;
#endif
        set_env("B3PORT_RENDER", lib);
    }
    {
        const char* given = std::getenv("B3PORT_RENDER_ARGS");
        std::string args = given ? std::string(given) : o.args;
        if (!given) {
            const char* gd = std::getenv("B3_GAME_DIR");
            const std::string game = !o.game.empty() ? o.game : gd && *gd ? std::string(gd) : std::string("data/game");
            if (args.find("--game ") == std::string::npos) args = "--game \"" + game + "\" " + args;
            if (o.mode == "window" && args.find("--activate") == std::string::npos) args += " --activate";
        }
        if (o.mode == "offscreen" && args.find("--offscreen-live") == std::string::npos) args += " --offscreen-live build/gameview";
        set_env("B3PORT_RENDER_ARGS", args);
    }
    g_vblank = o.vblank;
    b3gv::Host h;
    h.tick_stamp = stamp;
    h.extra_publish = publish_host;   // mac5: the PC SETTINGS page's text, the video skip hint
    b3gv::set_host(h);
    // the watch set: the HUD's entries and the poll point
    g_bits.assign(((b3host::kWatchHi - b3host::kWatchLo) >> 2) / 64 + 1, 0);
    std::size_t np = 0;
    const std::uint32_t* pcs = b3gv::entry_points(&np);
    for (std::size_t k = 0; k < np; ++k) set_bit(pcs[k]);
    set_bit(kUpdatePort);
    set_bit(kPresent);   // mac5
    set_bit(b3gv::kWorldRender);   // split2
    b3host::g_watch_bits = g_bits.data();
    b3host::g_watch_hook = watch;
    // Natively every listed entry is observable (the table's entries through dispatch; see §32.3 for direct links), so
    // the HUD capture is on whenever it wants entries.
    const std::uint32_t movie = b3gv::hud_enable(np, np, "native");
    g_on = true;
    std::printf("gameview: %s; %zu HUD entries watched, poll at UpdatePort(0) 0x%08X; movie pictures %s; renderer %s\n",
                o.mode.c_str(), np, kUpdatePort, movie ? "every poll" : "off", std::getenv("B3PORT_RENDER"));
    std::fflush(stdout);
    return true;
}

void b3gv_native::poll() {
    if (!g_on) return;
    ++g_poll;
    g_polled = true;
    // mac5: B3NATIVE_POLL_LOG=<first>-<last>: these polls' VBlanks logged (a shot's poll number to the game's time)
    static const std::pair<std::uint64_t, std::uint64_t> log_range = [] {
        unsigned long long lo = 0, hi = 0;
        if (const char* e = std::getenv("B3NATIVE_POLL_LOG")) std::sscanf(e, "%llu-%llu", &lo, &hi);
        return std::pair<std::uint64_t, std::uint64_t>(lo, hi);
    }();
    if (g_poll >= log_range.first && g_poll <= log_range.second && log_range.second != 0)
        std::printf("gameview: poll %llu at VBlank %llu\n", static_cast<unsigned long long>(g_poll),
                    static_cast<unsigned long long>(g_vblank ? g_vblank() : 0));
    if (g_frame_hook) g_frame_hook();   // sdk2b: the code mods' on_frame (their overlay for this frame)
    b3gv::on_poll(g_poll, g_vblank ? g_vblank() : 0, arena());
}

void b3gv_native::finish() {
    if (!g_on) return;
    g_on = false;
    b3host::g_watch_hook = nullptr;
    std::printf("gameview: %llu polls handed to the renderer\n", static_cast<unsigned long long>(g_poll));
    std::fflush(stdout);
    b3gv::finish();
}

bool b3gv_native::active() { return g_on; }

void b3gv_native::set_hold_probe(bool (*probe)()) { g_hold_probe = probe; }
void b3gv_native::set_skip_hint(std::uint8_t opacity) { g_skip_level = opacity; }
void b3gv_native::set_unstamped(bool on) { g_unstamped = on; }
void b3gv_native::set_menu_record(const std::vector<std::uint8_t>& bytes) { g_menu_record = bytes; g_menu_dirty = true; }
void b3gv_native::set_frame_hook(void (*fn)()) { g_frame_hook = fn; }
void b3gv_native::set_mod_overlay(const std::vector<std::uint8_t>& bytes) { g_mod_overlay = bytes; g_mod_overlay_dirty = true; }

bool b3gv_native::watch_extra(std::uint32_t address, void (*fn)(std::uint32_t, const std::uint32_t*)) {
    if (!g_on || g_bits.empty()) return false;
    for (Extra& x : g_extra)
        if (x.address == 0) {
            x = {address, fn};
            set_bit(address);
            return true;
        }
    return false;
}
