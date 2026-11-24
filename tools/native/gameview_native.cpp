// gameview_native.cpp -- the game view on the native backend (pic1; header gameview_native.h, docs/native-renderer.md §32).
#include "gameview_native.h"
#include "b3port/fileio.h"   // fsrobust
#include "render_load.h"

#include "b3gameview/gameview.h"
#include "b3host/native_backend.h"
#include "b3live/menu_input.h"
#include "b3live/sched_stamp.h"   // stutter: the stamp's late mark
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
// stutter: a paced game stamps with its VBlank's place on the pacer's schedule (set_vblank_stamp; fast_load.h
// Pacer::stamp_ns), steady by construction, bit 0 set when that VBlank started late (include/b3live/sched_stamp.h);
// unpaced (0) the time of the poll, as before (bit 0 clear).
bool g_unstamped = false;
std::uint64_t g_vblank_stamp = 0;
std::uint64_t stamp(std::uint64_t) {
    if (g_unstamped) return 0;
    if (g_vblank_stamp) return g_vblank_stamp;
    return b3live::sched_stamp(
        std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()), false);
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

// todrace (race_time.h): the race's DAY / NIGHT choice and the track page's label for the renderer (src/b3replay/hud.h
// kHostRaceTimeBase, 'HTD1': {u32 'HTD1', u32 serial, u32 race 0..2, u32 label: choice | opacity << 8, options3: u32 the
// records word's bits 32..63, u32 0}), set per VBlank
// by native_play, published at the poll when it changed. Host bytes only; none before the first set_race_time.
constexpr std::uint32_t kHostRaceTimeBase = 0x41112200u;
std::uint32_t g_race_time = 0, g_race_label = 0, g_race_serial = 0;
std::uint64_t g_race_options = 0;   // raceopts: the records word (low 16 bits in the label word's high half, 16..31 in the race word's, 32..63 in word 4)
std::uint32_t g_game_options = 0;   // gameopts: the race word's bits 9..10
bool g_race_set = false, g_race_dirty = false;
void publish_race_time() {
    if (!g_race_dirty) return;
    g_race_dirty = false;
    // (options3: the records word's bits 16..31 in the race word's 16..31 (gameopts' in 9..10, the race 0..2 in 0..7), its
    // bits 32..63 in word 4; every field's owner: tests/test_bit_owners.cpp)
    const std::uint32_t r[6] = {0x31445448u, ++g_race_serial,
                                g_race_time | g_game_options | std::uint32_t(g_race_options >> 16 & 0xFFFFu) << 16,
                                g_race_label | std::uint32_t(g_race_options & 0xFFFFu) << 16, std::uint32_t(g_race_options >> 32), 0};   // 'HTD1'
    b3scene_grab_host(kHostRaceTimeBase, reinterpret_cast<const std::uint8_t*>(r), 24);
}

// paintsel (paint_select.h): the PAINT prompt on SELECT CAR (src/b3replay/hud.h kHostPaintSelBase, 'HPS1': {u32 'HPS1', u32
// serial, u32 opacity 0..255, u32 player}), set per VBlank by native_play, published at the poll when it changed. Host
// bytes only; none before the prompt first shows.
constexpr std::uint32_t kHostPaintSelBase = 0x41112300u;
std::uint32_t g_paint_opacity = 0, g_paint_player = 0, g_paint_serial = 0;
bool g_paint_dirty = false;
// elim1 (single_eliminator.h): TIMED ELIMINATION's countdown (src/b3replay/hud.h kHostElimTimerBase, 'HEL1': {u32 'HEL1',
// u32 serial, u32 the whole seconds left (0xFFFFFFFF: none), u32 0, u32 colours[8] as RGBA bytes (red in the low byte):
// the race HUD's +0x30, +0x40, +0x50, +0x60, then the HUD captions' style 1 ramp c0..c3}), published when it changes
std::uint32_t g_elim_seconds = 0xFFFFFFFFu, g_elim_serial = 0, g_elim_colours[8] = {};
bool g_elim_dirty = false;
void publish_elim_timer() {
    if (!g_elim_dirty) return;
    g_elim_dirty = false;
    std::uint32_t r[12] = {0x314C4548u, ++g_elim_serial, g_elim_seconds, 0};   // 'HEL1'
    for (int i = 0; i < 8; ++i) r[4 + i] = g_elim_colours[i];
    b3scene_grab_host(0x41112400u, reinterpret_cast<const std::uint8_t*>(r), 48);
}
// gearbox (gearbox.h): the manual gearbox's gear for the HUD (src/b3replay/hud.h kHostGearBase, 'HGB1': {u32 'HGB1', u32
// serial, s32 the gear (-1 reverse, 0 neutral, 1..; -2 none), u32 0, u32 the race HUD's colours as the 'HEL1' record's}),
// published when it changes
std::int32_t g_gear = -2;
std::uint32_t g_gear_serial = 0, g_gear_colours[8] = {};
bool g_gear_dirty = false;
void publish_gear() {
    if (!g_gear_dirty) return;
    g_gear_dirty = false;
    std::uint32_t r[12] = {0x31424748u, ++g_gear_serial, std::uint32_t(g_gear), 0};   // 'HGB1'
    for (int i = 0; i < 8; ++i) r[4 + i] = g_gear_colours[i];
    b3scene_grab_host(0x41112500u, reinterpret_cast<const std::uint8_t*>(r), 48);
}
// replayview (replay_view.h): the race replay's state for the renderer (src/b3replay/hud.h kHostReplayBase, 'HRV1': {u32
// 'HRV1', u32 serial, u32 state: 0 none, 1 REPLAY offered (a results page), 2 the replay playing, u32 0}), set per VBlank
// by native_play, published when it changes
std::uint32_t g_replay_state = 0, g_replay_serial = 0;
bool g_replay_dirty = false;
void publish_replay() {
    if (!g_replay_dirty) return;
    g_replay_dirty = false;
    const std::uint32_t r[4] = {0x31565248u, ++g_replay_serial, g_replay_state, 0};   // 'HRV1'
    b3scene_grab_host(0x41112800u, reinterpret_cast<const std::uint8_t*>(r), 16);
}
void publish_paint_sel() {
    if (!g_paint_dirty) return;
    g_paint_dirty = false;
    const std::uint32_t r[4] = {0x31535048u, ++g_paint_serial, g_paint_opacity, g_paint_player};   // 'HPS1'
    b3scene_grab_host(kHostPaintSelBase, reinterpret_cast<const std::uint8_t*>(r), 16);
}

// paint3 (carousel_models.h): the carousel slots whose model is not their picture's (src/b3replay/hud.h kHostCarouselBase)
constexpr std::uint32_t kHostCarouselBase = 0x41112600u;
std::vector<std::uint8_t> g_carousel;
bool g_carousel_dirty = false;
void publish_carousel() {
    if (!g_carousel_dirty || g_carousel.empty() || g_carousel.size() > 0x200u) return;
    g_carousel_dirty = false;
    std::vector<std::uint8_t> b = g_carousel;
    b.resize((b.size() + 15) & ~std::size_t(15), 0);   // (16-byte rows)
    b3scene_grab_host(kHostCarouselBase, b.data(), std::uint32_t(b.size()));
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
    publish_race_time();
    publish_paint_sel();   // paintsel
    publish_carousel();    // paint3
    publish_elim_timer();  // elim1
    publish_gear();        // gearbox
    publish_replay();      // replayview
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
#ifdef _WIN32   // dll1114: the exe's own folder as UTF-8 (argv[0] is ANSI: a folder outside the code page broke it)
        const std::string dir = b3native::render_load::module_dir_utf8();
        const std::string lib = o.lib.empty() ? (dir.empty() ? exe_dir + "/b3render.dll" : dir + "\\b3render.dll") : o.lib;
#else
        const std::string lib = o.lib.empty() ? exe_dir + "/libb3render.so" : o.lib;
#endif
        set_env("B3PORT_RENDER", lib);
    }
    {
        const char* given = std::getenv("B3PORT_RENDER_ARGS");
        std::string args = given ? std::string(given) : o.args;
        if (!given) {
            const auto gd = b3io::getenv("B3_GAME_DIR");   // fsrobust: UTF-8
            const std::string game = !o.game.empty() ? o.game : gd ? *gd : std::string("data/game");
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
void b3gv_native::set_vblank_stamp(std::uint64_t ns, bool late) { g_vblank_stamp = b3live::sched_stamp(ns, late); }
void b3gv_native::set_ticker_text(std::uint32_t text, const std::u16string& display) { b3gv::set_ticker_text(text, display); }
void b3gv_native::set_race_time(std::uint8_t race, std::uint8_t label_choice, std::uint8_t label_opacity) {   // todrace
    const std::uint32_t label = std::uint32_t(label_choice) | std::uint32_t(label_opacity) << 8;
    if (g_race_set && race == g_race_time && label == g_race_label) return;
    g_race_set = true;
    g_race_time = race;
    g_race_label = label;
    g_race_dirty = true;
}
void b3gv_native::set_game_options(std::uint32_t latched) {   // gameopts
    const std::uint32_t w = std::uint32_t(latched & 3u) << 9;
    if (g_game_options == w) return;
    g_game_options = w;
    g_race_dirty = true;
}
void b3gv_native::set_race_options(std::uint64_t word) {   // raceopts
    if (g_race_options == word) return;
    g_race_options = word;
    g_race_dirty = true;
}
void b3gv_native::set_replay_state(int state) {   // replayview
    const std::uint32_t st = std::uint32_t(state < 0 ? 0 : state > 2 ? 2 : state);
    if (st == g_replay_state) return;
    g_replay_state = st;
    g_replay_dirty = true;
}
void b3gv_native::set_paint_prompt(std::uint8_t opacity, std::uint8_t player) {   // paintsel
    if (g_paint_opacity == opacity && g_paint_player == player) return;
    if (!g_paint_serial && !g_paint_dirty && opacity == 0) return;   // nothing before it first shows
    g_paint_opacity = opacity;
    g_paint_player = player;
    g_paint_dirty = true;
}
void b3gv_native::set_gear(int gear, const std::uint32_t colours[8]) {   // gearbox
    bool same = gear == g_gear;
    for (int i = 0; i < 8 && colours; ++i) same = same && g_gear_colours[i] == colours[i];
    if (same) return;
    if (!g_gear_serial && !g_gear_dirty && gear < -1) return;   // nothing before it first shows
    g_gear = gear;
    for (int i = 0; i < 8 && colours; ++i) g_gear_colours[i] = colours[i];
    g_gear_dirty = true;
}
void b3gv_native::set_elim_timer(int seconds, const std::uint32_t colours[8]) {   // elim1
    const std::uint32_t v = seconds < 0 ? 0xFFFFFFFFu : std::uint32_t(seconds);
    bool same = v == g_elim_seconds;
    for (int i = 0; i < 8 && colours; ++i) same = same && g_elim_colours[i] == colours[i];
    if (same) return;
    g_elim_seconds = v;
    for (int i = 0; i < 8 && colours; ++i) g_elim_colours[i] = colours[i];
    g_elim_dirty = true;
}
void b3gv_native::set_carousel_models(const std::vector<std::uint8_t>& record) {   // paint3
    if (record.size() < 16 || record == g_carousel) return;
    g_carousel = record;
    g_carousel_dirty = true;
}
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
