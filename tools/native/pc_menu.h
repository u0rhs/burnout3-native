// pc_menu.h -- native_play's own menu rows: "SETTINGS" and "QUIT GAME" after the main menu's five, and "SETTINGS" in the
// race's pause menu before its last row (mac5, 2026-11-09; docs/pc-settings.md;
// 2026-11-09: called SETTINGS, and reachable in a race).
//
// The main menu page (D_01E94C40, func_00318340 Prepare / func_003181E0 Update) holds its five rows in arrays sized five
// (rows at +0x18, 8 bytes each; help texts at +0x40, 4 bytes each; count at +0x16). Its Prepare hands them to the
// selection component (Prepare__19CB3VSelectComponent 0x321470, D_01E89544) and the help bar
// (Prepare__19CB3HelpBarComponent 0x31C350, D_01E89788). Three wrappers on the hook table (hook_table.h, port layer):
//   - the two Prepares, called for the main menu's own components: 7-row copies in free guest RAM (the mod arena's
//     port region GuestArena::kPcMenu, 0xC0000, below the ELF; checked zero before first use) with our two rows, and
//     the count 7;
//   - the main menu's Update: Confirm on row 5 opens PC SETTINGS (the renderer's page), on row 6 quits; the game's code
//     would read its 5-row array past the end there. Every other row and input runs the game's code unchanged.
// The pause page (D_01E94C94, func_00342410 Prepare / func_00342090 Update) builds up to six rows of its seven slots
// (+8, 8 bytes each) with an action code per row (+0x44; the object's +0x5C pad is the seventh code, used by no pause
// code), and hands them to the same selection component. For its race form (+0x60 clear: action codes, not row
// indices, pick the action) the selection Prepare wrapper inserts SETTINGS before the last row with action code 6,
// which the game's switch ignores; the Update wrapper opens the page on Confirm there.
// photorow1 (2026-11-15): a PHOTO MODE row after SETTINGS (PC SETTINGS > GAME > INTERFACE > PHOTO MODE IN PAUSE MENU; B3_PHOTO_ROW=0|1
// overrides it: golden runs hide it). The page's slots are full with SETTINGS (seven; the action codes follow at +0x44 and the
// race flag at +0x60), so the selection gets a private array of up to eight rows (the page's, SETTINGS, PHOTO MODE, the last
// row) and the count one higher. The Update wrapper takes Confirm on the PHOTO MODE row (photo mode: b3r_photo_enter, as P),
// and for the rows after it shows the game's Update the index one lower (its slot tables are the page's own).
// Not shown in the 2-player split screen (CB3Game::Is2PlayerSplitScreen, func_00131CA0): photo mode is one view's.
// B3NATIVE_PC_ROW=0 leaves both menus as retail.
#ifndef B3NATIVE_PC_MENU_H
#define B3NATIVE_PC_MENU_H

#include "b3host/call.h"
#include "b3host/host.h"
#include "b3settings/i18n.h"   // 1.3.1: the main menu's help texts in the LANGUAGE
#include "hook_table.h"
#include "pause_rows.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace b3native {

class PcMenu {
public:
    // Off with B3NATIVE_PC_ROW=0.
    static bool requested();
    // On the hook table's port layer (native_play installs it first of the port's wrappers).
    bool install(HookTable& hooks);
    // Set by the Update wrapper; the caller acts on them (and clears them) once per VBlank.
    bool take_open_request() { const bool r = open_requested_; open_requested_ = false; return r; }
    bool quit_requested() const { return quit_requested_; }
    // photorow1: whether the pause menu gets the PHOTO MODE row now (called on the game thread by the pause page's selection
    // Prepare); unset: no row. The Update wrapper sets the request when Confirm is on the row; the caller acts on it.
    void set_photo_row_query(std::function<bool()> f) { photo_row_query_ = std::move(f); }
    // 1.3.1: the main menu's two help texts. The game lays out (and spends its time on) the English ones 1.3.0 had, at the
    // same guest addresses, so a run is as before (its help bar's draw cost follows the text's words: a longer text made
    // the online tour run apart); the renderer shows this version's words in the LANGUAGE in their place: `sink` gets
    // (guest text, the text shown; empty: the game's own) at each Prepare (native_play: b3gv_native::set_ticker_text).
    // `language`: the code of the game's text (platform_disc.h language_text(); "" English).
    void set_help_text(std::string language, void (*sink)(b3host::gaddr, const std::u16string&)) {
        language_ = std::move(language);
        help_sink_ = sink;
    }
    bool take_photo_request() { const bool r = photo_requested_; photo_requested_ = false; return r; }
    // the row's place in the page just prepared (0: none); tests and logs
    unsigned photo_row_index() const { return photo_shown_ ? photo_idx_ : 0u; }
    // photo mode (src/b3replay/photomode.h): the pause page's Update calls so far (it runs every frame the race's pause
    // menu is up; native_play tells the renderer while it does)
    std::uint64_t pause_updates() const { return pause_updates_; }

private:
    static PcMenu*& instance();
    static void select_prepare(const b3host::CallArgs& a, b3host::CallResult& r);
    static void help_prepare(const b3host::CallArgs& a, b3host::CallResult& r);
    static void update(const b3host::CallArgs& a, b3host::CallResult& r);
    static void pause_update(const b3host::CallArgs& a, b3host::CallResult& r);
    static void add_pause_row();
    static b3host::gaddr add_photo_row(PcMenu* p, std::uint32_t sp);
    bool ensure_strings();

    void (*select_prepare_)(const b3host::CallArgs&, b3host::CallResult&) = nullptr;
    void (*help_prepare_)(const b3host::CallArgs&, b3host::CallResult&) = nullptr;
    void (*update_)(const b3host::CallArgs&, b3host::CallResult&) = nullptr;
    void (*pause_update_)(const b3host::CallArgs&, b3host::CallResult&) = nullptr;
    bool strings_ = false, region_bad_ = false;
    bool open_requested_ = false, quit_requested_ = false, photo_requested_ = false;
    std::function<bool()> photo_row_query_;
    std::string language_;                                            // 1.3.1: the help texts' LANGUAGE ("" English)
    void (*help_sink_)(b3host::gaddr, const std::u16string&) = nullptr;
    bool photo_shown_ = false;                    // the page's rows are in the private array with the PHOTO MODE row
    unsigned photo_idx_ = 0, photo_count_ = 0;    // its index, and the count the page holds with it
    bool photo_logged_ = false;
    std::uint64_t pause_updates_ = 0;
};

}  // namespace b3native

// ---- implementation (header-only: native_play.cpp is its one includer) ----
namespace b3native {
namespace pc_menu_detail {
constexpr b3host::gaddr kMainMenu = 0x01E94C40u;      // the main menu page [C func_00318340's self]
constexpr b3host::gaddr kSelect = 0x01E89544u, kHelpBar = 0x01E89788u;   // its components [C func_00318340]
constexpr b3host::gaddr kSelectPrepare = 0x00321470u, kHelpPrepare = 0x0031C350u, kUpdate = 0x003181E0u;
constexpr b3host::gaddr kGetMenuConfirm = 0x0022CFE0u, kInputManager = 0x004F50C0u;   // [C func_003181E0]
// Free guest RAM below the ELF, the mod arena's port region (checked zero before first use, see the top): 7 rows x 8,
// 7 help texts x 4, then UTF-16 strings
constexpr b3host::gaddr kRegion = GuestArena::kPcMenu.address, kRows = kRegion, kHelp = kRegion + 0x40u,
                        kStrings = kRegion + 0x80u, kRegionEnd = kRegion + GuestArena::kPcMenu.size;
static_assert(kRegion == 0x000C0000u && kRegionEnd == 0x000C0400u, "pc_menu's rows stay where they were");
constexpr unsigned kGameRows = 5, kRowsPc = 7, kRowPcSettings = 5, kRowQuit = 6;
constexpr b3host::gaddr kPausePage = 0x01E94C94u, kPauseUpdate = 0x00342090u;   // [C B3PauseState 0x3428D0's func_00342410 call]
constexpr unsigned kPauseSlots = 7, kPauseSettingsAction = 6;   // its row slots; an action code its Update ignores
constexpr b3host::gaddr kPauseLabel = kStrings + 0x180u;
constexpr b3host::gaddr kPhotoLabel = kStrings + 0x1C0u, kPhotoRows = kRegion + 0x300u;   // photorow1: 8 rows x 8 (to +0x340)
constexpr b3host::gaddr kIs2PlayerSplit = 0x00131CA0u, kGame = 0x004EE040u;   // CB3Game::Is2PlayerSplitScreen(&D_004EE040)
static_assert(kPhotoRows + 8u * 8u <= kRegionEnd && kPhotoRows >= kStrings + 0x200u, "the PHOTO MODE rows fit the region");

inline b3host::gaddr write_utf16(b3host::gaddr at, const char* text) {
    for (const char* c = text; *c; ++c, at += 2) b3host::store<std::uint16_t>(at, static_cast<std::uint8_t>(*c));
    b3host::store<std::uint16_t>(at, 0);
    return (at + 2 + 3) & ~3u;
}
inline u_long128 reg(std::uint32_t v) { u_long128 q{}; q.w[0] = v; return q; }
// 1.3.1: the help texts. kGame*: what the game lays out (1.3.0's words, kept for its timing: PcMenu::set_help_text);
// kShown*: the words on screen, in the LANGUAGE through the port's table (include/b3settings/i18n.h)
constexpr const char* kGamePcHelp = "DISPLAY, GRAPHICS, EFFECTS AND CAMERA";
constexpr const char* kGameQuitHelp = "LEAVE THE GAME";
constexpr const char* kShownPcHelp = "DISPLAY, GRAPHICS, CAR, CONTROLS AND MORE";
constexpr const char* kShownQuitHelp = "LEAVE THE GAME";
inline const char* port_text(const char* en, const std::string& language) {   // as b3settings::translate_text (exact entries)
    for (const b3settings::i18n::Table& t : b3settings::i18n::kTables)
        if (language == t.code)
            for (std::size_t k = 0; k < t.count; ++k)
                if (std::strcmp(t.entries[k].en, en) == 0) return t.entries[k].text;
    return en;
}
inline std::u16string utf16(const char* s) {   // UTF-8 -> UTF-16 (the tables' text: the BMP)
    std::u16string o;
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(s); *p;) {
        std::uint32_t c = *p++;
        int more = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
        c &= more == 3 ? 0x07u : more == 2 ? 0x0Fu : more == 1 ? 0x1Fu : 0x7Fu;
        for (; more > 0 && (*p & 0xC0u) == 0x80u; --more) c = c << 6 | (*p++ & 0x3Fu);
        o.push_back(c > 0xFFFFu ? u'?' : char16_t(c));
    }
    return o;
}
// i18n2: a row label the game lays out, in the LANGUAGE (the port's table, include/b3settings/i18n.h). English (or a
// language without the entry): the same bytes as before. `units`: the buffer's UTF-16 units, the terminator included (a
// longer text is cut there). Returns the next 4-aligned address, as write_utf16.
inline b3host::gaddr write_label(b3host::gaddr at, const char* en, const std::string& language, unsigned units) {
    if (language.empty()) return write_utf16(at, en);
    const std::u16string t = utf16(port_text(en, language));
    unsigned n = 0;
    for (; n < t.size() && n + 1 < units; ++n, at += 2) b3host::store<std::uint16_t>(at, static_cast<std::uint16_t>(t[n]));
    b3host::store<std::uint16_t>(at, 0);
    return (at + 2 + 3) & ~3u;
}
// the labels' room: the main menu's two share kStrings .. +0x80 (kHelp's texts follow), the pause page's SETTINGS and
// PHOTO MODE have 0x40 bytes each
constexpr unsigned kMainLabelUnits = 30, kPauseLabelUnits = 32;
static_assert(2u * (2u * kMainLabelUnits + 2u) <= 0x80u, "the main menu's labels stay before the help texts");
static_assert(kPhotoLabel - kPauseLabel == 2u * kPauseLabelUnits && kPhotoRows - kPhotoLabel >= 2u * kPauseLabelUnits, "the pause labels' room");
}  // namespace pc_menu_detail

inline bool PcMenu::requested() {
    const char* e = std::getenv("B3NATIVE_PC_ROW");
    return !(e && e[0] == '0' && e[1] == 0);
}

inline PcMenu*& PcMenu::instance() { static PcMenu* p = nullptr; return p; }

inline bool PcMenu::install(HookTable& hooks) {
    using namespace pc_menu_detail;
    const bool sp = hooks.has_row(kSelectPrepare), hp = hooks.has_row(kHelpPrepare), up = hooks.has_row(kUpdate);
    const bool pp = hooks.has_row(kPauseUpdate);
    if (!sp || !hp || !up) {
        std::fprintf(stderr, "pc_menu: the main menu's functions are not all native (%d %d %d): menu left as retail\n",
                     sp, hp, up);
        return false;
    }
    std::vector<Hook> set = {{kSelectPrepare, HookLayer::kPort, &select_prepare, &select_prepare_},
                             {kHelpPrepare, HookLayer::kPort, &help_prepare, &help_prepare_},
                             {kUpdate, HookLayer::kPort, &update, &update_}};
    if (pp) set.push_back({kPauseUpdate, HookLayer::kPort, &pause_update, &pause_update_});
    std::string error;
    if (!hooks.install("port:pc_menu", set, &error)) {
        std::fprintf(stderr, "pc_menu: %s: menu left as retail\n", error.c_str());
        return false;
    }
    instance() = this;
    std::printf("pc_menu: main menu rows SETTINGS and QUIT GAME%s (B3NATIVE_PC_ROW=0: retail's menus)\n",
                pp ? ", pause menu row SETTINGS" : " (the pause menu's Update is not native: no pause row)");
    return true;
}

inline bool PcMenu::ensure_strings() {
    using namespace pc_menu_detail;
    if (strings_) return true;
    if (region_bad_) return false;
    for (b3host::gaddr a = kRegion; a < kRegionEnd; a += 4)
        if (b3host::load<std::uint32_t>(a) != 0) {
            std::fprintf(stderr, "pc_menu: guest RAM %08X..%08X is in use (%08X): menu left as retail\n", kRegion, kRegionEnd, a);
            region_bad_ = true;
            return false;
        }
    strings_ = true;
    return true;
}

// The race pause page's rows, in place before the selection reads them: SETTINGS before the last row (once per Prepare:
// a second selection Prepare without the page's own finds it there).
inline void PcMenu::add_pause_row() {
    using namespace pc_menu_detail;
    if (b3host::load<std::uint8_t>(kPausePage + 0x60u) != 0) return;   // the other form: rows picked by index
    const unsigned n = b3host::load<std::uint16_t>(kPausePage + 6u);
    if (n < 2 || n >= kPauseSlots) return;
    auto row = [](unsigned i) { return kPausePage + 8u + 8u * i; };
    auto action = [](unsigned i) { return kPausePage + 0x44u + 4u * i; };
    for (unsigned i = 0; i < n; ++i)
        if (b3host::load<std::uint32_t>(row(i)) == kPauseLabel) return;
    write_label(kPauseLabel, "SETTINGS", instance() ? instance()->language_ : std::string(), kPauseLabelUnits);   // i18n2
    b3host::store<std::uint32_t>(row(n), b3host::load<std::uint32_t>(row(n - 1)));
    b3host::store<std::uint32_t>(row(n) + 4u, b3host::load<std::uint32_t>(row(n - 1) + 4u));
    b3host::store<std::uint32_t>(action(n), b3host::load<std::uint32_t>(action(n - 1)));
    b3host::store<std::uint32_t>(row(n - 1), kPauseLabel);
    b3host::store<std::uint32_t>(row(n - 1) + 4u, 0);   // +4 enabled (0), +5 0, as the game's rows
    b3host::store<std::uint32_t>(action(n - 1), kPauseSettingsAction);
    b3host::store<std::uint16_t>(kPausePage + 6u, static_cast<std::uint16_t>(n + 1));
}

// photorow1: the pause page's rows (SETTINGS in place) copied to the private array with PHOTO MODE before the last row, the
// page's count one higher. Returns the array, or 0 (no row: setting off, split screen, an unexpected page). A second
// Prepare without the page's own (the count is ours already) finds the array as it was.
inline b3host::gaddr PcMenu::add_photo_row(PcMenu* p, std::uint32_t sp) {
    using namespace pc_menu_detail;
    const unsigned n = b3host::load<std::uint16_t>(kPausePage + 6u);
    if (p->photo_shown_ && n == p->photo_count_ && b3host::load<std::uint32_t>(kPhotoRows + 8u * (n - 2u)) == kPhotoLabel) return kPhotoRows;
    p->photo_shown_ = false;
    if (!p->photo_row_query_ || !p->photo_row_query_()) return 0;
    if (n > kPauseSlots) return 0;
    PauseRow page[kPauseSlots], out[kPauseSlots + 1];
    for (unsigned i = 0; i < n; ++i) {
        page[i].label = b3host::load<std::uint32_t>(kPausePage + 8u + 8u * i);
        page[i].flags = b3host::load<std::uint32_t>(kPausePage + 8u + 8u * i + 4u);
    }
    unsigned photo_idx = 0;
    if (!PhotoRows::build(page, n, kPauseLabel, kPhotoLabel, out, photo_idx)) return 0;   // SETTINGS, then the last row
    {
        b3host::CallArgs c{};
        c.gpr[0] = reg(kGame);
        c.n_gpr = 1;
        c.sp = sp;
        b3host::CallResult cr{};
        b3host::detail::dispatch(kIs2PlayerSplit, c, cr);
        if (cr.v0.w[0] & 0xFFu) return 0;
    }
    write_label(kPhotoLabel, "PHOTO MODE", p->language_, kPauseLabelUnits);   // i18n2
    for (unsigned i = 0; i <= n; ++i) {
        b3host::store<std::uint32_t>(kPhotoRows + 8u * i, out[i].label);
        b3host::store<std::uint32_t>(kPhotoRows + 8u * i + 4u, out[i].flags);
    }
    p->photo_idx_ = photo_idx;
    p->photo_count_ = n + 1u;
    p->photo_shown_ = true;
    b3host::store<std::uint16_t>(kPausePage + 6u, static_cast<std::uint16_t>(n + 1u));
    return kPhotoRows;
}

// Prepare__19CB3VSelectComponent(this, selection data, rows, f12 width): for the main menu's selection, our 7 rows; for
// the race pause page's, its rows with SETTINGS added.
inline void PcMenu::select_prepare(const b3host::CallArgs& a, b3host::CallResult& r) {
    using namespace pc_menu_detail;
    PcMenu* p = instance();
    if ((a.gpr[0].w[0] & 0x01FFFFFFu) == kSelect && (a.gpr[1].w[0] & 0x01FFFFFFu) == kPausePage + 4u &&
        (a.gpr[2].w[0] & 0x01FFFFFFu) == kPausePage + 8u && p->pause_update_ && p->ensure_strings()) {
        add_pause_row();
        const b3host::gaddr rows = add_photo_row(p, a.sp);
        if (rows == 0) {
            p->select_prepare_(a, r);
            return;
        }
        b3host::CallArgs b = a;
        b.gpr[2] = reg(rows);
        p->select_prepare_(b, r);
        return;
    }
    if ((a.gpr[0].w[0] & 0x01FFFFFFu) != kSelect || (a.gpr[1].w[0] & 0x01FFFFFFu) != kMainMenu + 0x14u || !p->ensure_strings()) {
        p->select_prepare_(a, r);
        return;
    }
    const b3host::gaddr game_rows = a.gpr[2].w[0] & 0x01FFFFFFu;
    for (unsigned k = 0; k < kGameRows * 8u; k += 4) b3host::store<std::uint32_t>(kRows + k, b3host::load<std::uint32_t>(game_rows + k));
    b3host::gaddr s = kStrings;
    const b3host::gaddr pc_label = s;   s = write_label(s, "SETTINGS", p->language_, kMainLabelUnits);   // i18n2: in the LANGUAGE
    const b3host::gaddr quit_label = s; s = write_label(s, "QUIT GAME", p->language_, kMainLabelUnits);
    b3host::store<std::uint32_t>(kRows + kRowPcSettings * 8u, pc_label);
    b3host::store<std::uint32_t>(kRows + kRowPcSettings * 8u + 4u, 0);   // +4 enabled (0), +5 0, as the game's rows
    b3host::store<std::uint32_t>(kRows + kRowQuit * 8u, quit_label);
    b3host::store<std::uint32_t>(kRows + kRowQuit * 8u + 4u, 0);
    b3host::store<std::uint16_t>(kMainMenu + 0x16u, static_cast<std::uint16_t>(kRowsPc));   // the selection's count
    b3host::CallArgs b = a;
    b.gpr[2] = reg(kRows);
    p->select_prepare_(b, r);
}

// Prepare__19CB3HelpBarComponent(this, selection data, help texts, bool, f12): for the main menu, our 7 help texts.
inline void PcMenu::help_prepare(const b3host::CallArgs& a, b3host::CallResult& r) {
    using namespace pc_menu_detail;
    PcMenu* p = instance();
    if ((a.gpr[0].w[0] & 0x01FFFFFFu) != kHelpBar || (a.gpr[1].w[0] & 0x01FFFFFFu) != kMainMenu + 0x14u || !p->strings_) {
        p->help_prepare_(a, r);
        return;
    }
    const b3host::gaddr game_help = a.gpr[2].w[0] & 0x01FFFFFFu;
    for (unsigned k = 0; k < kGameRows; ++k) b3host::store<std::uint32_t>(kHelp + 4u * k, b3host::load<std::uint32_t>(game_help + 4u * k));
    b3host::gaddr s = kStrings + 0x80u;
    const b3host::gaddr pc_help = s;   s = write_utf16(s, kGamePcHelp);
    const b3host::gaddr quit_help = s; s = write_utf16(s, kGameQuitHelp);
    if (p->help_sink_)   // 1.3.1: the words shown (set_help_text); the game's own where they are the same
        for (const auto& [at, game, shown] : {std::tuple{pc_help, kGamePcHelp, kShownPcHelp}, std::tuple{quit_help, kGameQuitHelp, kShownQuitHelp}}) {
            const std::u16string text = utf16(port_text(shown, p->language_));
            p->help_sink_(at, text == utf16(game) ? std::u16string() : text);
        }
    b3host::store<std::uint32_t>(kHelp + 4u * kRowPcSettings, pc_help);
    b3host::store<std::uint32_t>(kHelp + 4u * kRowQuit, quit_help);
    b3host::CallArgs b = a;
    b.gpr[2] = reg(kHelp);
    p->help_prepare_(b, r);
}

// func_003181E0 (the main menu's Update): Confirm on our rows is ours; everything else is the game's.
inline void PcMenu::update(const b3host::CallArgs& a, b3host::CallResult& r) {
    using namespace pc_menu_detail;
    PcMenu* p = instance();
    const b3host::gaddr self = a.gpr[0].w[0] & 0x01FFFFFFu;
    const unsigned row = self == kMainMenu && p->strings_ ? b3host::load<std::uint16_t>(kMainMenu + 0x14u) : 0u;
    if (row == kRowPcSettings || row == kRowQuit) {
        b3host::CallArgs c{};
        c.gpr[0] = reg(kInputManager);
        c.gpr[1] = reg(0);
        c.n_gpr = 2;
        c.sp = a.sp;
        b3host::CallResult cr{};
        b3host::detail::dispatch(kGetMenuConfirm, c, cr);
        if (cr.v0.w[0] & 0xFFu) {
            if (row == kRowPcSettings) p->open_requested_ = true;
            else p->quit_requested_ = true;
            r.v0 = reg(1);
            return;
        }
    }
    p->update_(a, r);
}

// func_00342090 (the pause page's Update, a1 the menu input): Confirm on our row opens the page; the rest is the game's.
inline void PcMenu::pause_update(const b3host::CallArgs& a, b3host::CallResult& r) {
    using namespace pc_menu_detail;
    PcMenu* p = instance();
    const b3host::gaddr self = a.gpr[0].w[0] & 0x01FFFFFFu, input = a.gpr[1].w[0] & 0x01FFFFFFu;
    if (self == kPausePage) ++p->pause_updates_;
    if (self == kPausePage && p->strings_ && b3host::load<std::uint8_t>(kPausePage + 0x60u) == 0 &&
        b3host::load<std::int16_t>(input + 0x50u) != 0) {
        const unsigned sel = b3host::load<std::uint16_t>(kPausePage + 4u);
        const bool photo = p->photo_shown_ && b3host::load<std::uint16_t>(kPausePage + 6u) == p->photo_count_;
        const int page_idx = photo ? PhotoRows::page_index(sel, p->photo_idx_) : int(sel);
        if (page_idx < 0) {   // photorow1: Confirm on PHOTO MODE
            p->photo_requested_ = true;
            return;
        }
        const unsigned idx = unsigned(page_idx);   // the rows after it are the page's, one up
        if (idx < kPauseSlots && b3host::load<std::uint32_t>(kPausePage + 0x44u + 4u * idx) == kPauseSettingsAction) {
            p->open_requested_ = true;
            return;
        }
        if (idx != sel) {   // the game's Update reads its tables at the index: the last row's slot
            b3host::store<std::uint16_t>(kPausePage + 4u, static_cast<std::uint16_t>(idx));
            p->pause_update_(a, r);
            b3host::store<std::uint16_t>(kPausePage + 4u, static_cast<std::uint16_t>(sel));
            return;
        }
    }
    p->pause_update_(a, r);
}

}  // namespace b3native

#endif
