// pcpage.cpp -- the PC SETTINGS page in the native renderer (pcpage.h).
#include "pcpage.h"
#include "b3port/file_url.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <string_view>
#include <system_error>
#include <vector>

#include <SDL3/SDL.h>

#include "b3asset/language.h"   // 1.0.1
#include "b3port/fileio.h"   // fsrobust
#include "b3port/user_dir.h"   // portable mode (1.3.0)
#include "b3asset/hdpack.h"
#include "../native/pcsx2_cards.h"   // 1.1: IMPORT SAVE starts in PCSX2's memory cards
#include "tuneconsole.h"   // tune4: the DEVELOPER tab
#include "nativepad.h"
#include "glyphdraw.h"   // settingsui: the prompt bar's glyphs

namespace b3replay {
namespace {

// The pad model's 32-byte sample (sif::set_pad_sample, NativePad::sample): the buttons first, libpad2 order, active low
// (byte i / 8, bit i % 8), then the sticks RX RY LX LY (0x80 centre) [C native_play's pad log: Start = F7 FF 80 7F 80 7F]
bool pressed(const std::array<std::uint8_t, 32>& r, unsigned i) { return (r[i / 8] & (1u << (i % 8))) == 0; }
enum : unsigned { kStart = 3, kUp = 4, kRight = 5, kDown = 6, kLeft = 7, kL2 = 8, kR2 = 9, kL1 = 10, kR1 = 11, kTriangle = 12, kCross = 14, kSquare = 15 };

// devnav1 (2026-11-13): B3_UI_TIMING=1: the page's own cost in the log, every 120 frames (the DEVELOPER tab's rows,
// the whole input-and-rows step, the drawing) and at each focus move (a player report: the DEVELOPER tab took seconds
// a step on a Mac)
struct UiTiming {
    const bool on = [] { const char* e = std::getenv("B3_UI_TIMING"); return e && *e && *e != '0'; }();
    double sum[3] = {}, max[3] = {};
    int n[3] = {};
    std::size_t last_focus = std::size_t(-1), last_tab = std::size_t(-1);
    void add(int k, double ms) {
        sum[k] += ms;
        max[k] = std::max(max[k], ms);
        ++n[k];
    }
    void report(std::size_t tab, std::size_t rows) {
        if (n[1] < 120) return;
        std::printf("[b3ui] PC SETTINGS, %d frames, tab %zu (%zu rows): DEVELOPER rows %.3f ms (max %.3f), the step %.3f ms (max %.3f), "
                    "the drawing %.3f ms (max %.3f, %d)\n", n[1], tab, rows, n[0] ? sum[0] / n[0] : 0.0, max[0], sum[1] / n[1], max[1],
                    n[2] ? sum[2] / n[2] : 0.0, max[2], n[2]);
        std::fflush(stdout);
        for (int k = 0; k < 3; ++k) { sum[k] = max[k] = 0; n[k] = 0; }
    }
};
UiTiming& ui_timing() {
    static UiTiming t;
    return t;
}
double ms_since(std::uint64_t t0) { return double(SDL_GetTicksNS() - t0) * 1e-6; }

// UTF-8 to the game's 16-bit codes (Unicode, as its European text has them: the font's accented letters). A byte
// that does not start a well-formed sequence stands for itself (Latin-1), as before 1.1.
// 1.2.1: a Latin-1 letter the game's font has no glyph for (the US disc's HeadFont has no È: "PARAM□TRES") drawn as its
// base letter(s) instead of the game's fallback box; letters the font has are kept. Our own text only (PC SETTINGS).
std::u16string fit_to_font(const HostFont& f, std::u16string s) {
    static const char* const kBase[64] = {   // U+00C0 .. U+00FF, case kept; "" = no fallback
        "A", "A", "A", "A", "A", "A", "AE", "C", "E", "E", "E", "E", "I", "I", "I", "I", "D", "N", "O", "O", "O", "O", "O", "",
        "O", "U", "U", "U", "U", "Y", "TH", "ss", "a", "a", "a", "a", "a", "a", "ae", "c", "e", "e", "e", "e", "i", "i", "i", "i",
        "d", "n", "o", "o", "o", "o", "o", "", "o", "u", "u", "u", "u", "y", "th", "y"};
    const auto has = [&f](char16_t c) {
        for (const HostFontGlyph& g : f.glyphs)
            if (g.code == c) return true;
        return false;
    };
    std::u16string out;
    out.reserve(s.size());
    for (const char16_t c : s) {
        if (c >= 0xC0 && c <= 0xFF && *kBase[c - 0xC0] && !has(c))
            for (const char* b = kBase[c - 0xC0]; *b; ++b) out += char16_t(*b);
        else out += c;
    }
    return out;
}

std::u16string to_u16(std::string_view s) {
    std::u16string o;
    o.reserve(s.size());
    for (std::size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        const auto cont = [&](std::size_t k) { return i + k < s.size() && (static_cast<unsigned char>(s[i + k]) & 0xC0) == 0x80; };
        if (c >= 0xC2 && c < 0xE0 && cont(1)) {
            o.push_back(char16_t((c & 0x1F) << 6 | (static_cast<unsigned char>(s[i + 1]) & 0x3F)));
            i += 2;
        } else if (c >= 0xE0 && c < 0xF0 && cont(1) && cont(2)) {
            const unsigned v = (c & 0x0Fu) << 12 | (static_cast<unsigned char>(s[i + 1]) & 0x3Fu) << 6 | (static_cast<unsigned char>(s[i + 2]) & 0x3Fu);
            o.push_back(v >= 0x800 ? char16_t(v) : u'?');
            i += 3;
        } else {
            o.push_back(char16_t(c));
            ++i;
        }
    }
    return o;
}

}  // namespace

std::u16string host_font_text(const HostFont& f, std::string_view utf8) { return fit_to_font(f, to_u16(utf8)); }   // todrace

void PcPage::set_mod_rows(const std::string& text) {   // sdk2b
    std::vector<b3mod::SettingRow> rows = b3mod::rows_from_text(text);
    std::lock_guard<std::mutex> lock(mod_m_);
    mod_rows_ = std::move(rows);
}

void PcPage::set_mod_value(const std::string& qualified, const std::string& value) {
    std::lock_guard<std::mutex> lock(mod_m_);
    for (b3mod::SettingRow& r : mod_rows_)
        if (r.qualified() == qualified)
            if (const int v = b3mod::value_from_text(r, value); v >= 0) r.value = v;
}

int PcPage::take_mod_change(char* out, int n) {
    std::lock_guard<std::mutex> lock(mod_m_);
    if (mod_changes_.empty() || !out || n <= 1) return 0;
    const std::string c = mod_changes_.front();
    mod_changes_.pop_front();
    const int k = std::min(int(c.size()), n - 1);
    std::memcpy(out, c.data(), std::size_t(k));
    out[k] = 0;
    return k;
}

PcPage& PcPage::instance() {
    static PcPage* p = new PcPage();   // never destroyed (the render thread may outlive static destruction)
    return *p;
}

bool PcPage::frame(const std::array<std::uint8_t, 32>& pad, float seconds, const std::string& ini, const MenuMouse::PagePointer* mouse,
                   float aspect) {
    publish_music();
    if (open_requested_.exchange(false) && !active_.load()) {
        menu_.open(menu_.tab());
        active_.store(true);
        visit_ = settings_;          // nextstart: what the question box compares with at the close
        visit_capture_ = true;
        ask_open_ = false;
        std::printf("[b3settings] PC SETTINGS opened\n");
        std::fflush(stdout);
    }
    const bool paint_changed = paint_step(ini);   // paintsel
    if (!active_.load()) return paint_changed;
    if (paint_open_.load()) {
        paint_aspect_ = aspect;   // paint2: the picker's pointer
        return paint_frame(pad, seconds, ini, mouse) || paint_changed;
    }
    UiTiming& timing = ui_timing();
    const std::uint64_t t_frame = timing.on ? SDL_GetTicksNS() : 0;
    b3settings::Input in;
    // the left stick (sample bytes 4 / 5, 0x80 centre) as the D-pad, as the game's menus take it
    const int lx = int(pad[4]) - 0x80, ly = int(pad[5]) - 0x80;
    in.up = pressed(pad, kUp) || ly < -0x40;
    in.down = pressed(pad, kDown) || ly > 0x40;
    in.left = pressed(pad, kLeft) || lx < -0x40;
    in.right = pressed(pad, kRight) || lx > 0x40;
    in.confirm = pressed(pad, kCross);
    in.back = pressed(pad, kTriangle) || pressed(pad, kStart);
    in.previous_tab = pressed(pad, kL1);
    in.next_tab = pressed(pad, kR1);
    in.reset = pressed(pad, kSquare);   // rebind2: the focused control back to its default
    // devnav1: the previous / next section (a long tab: DEVELOPER): PageUp / PageDown, a controller's L2 / R2
    // settingsui: and a controller's right stick up / down (the PS2 report's L2 / R2 carry nothing in the menus on the
    // Xbox layout); the prompt bar shows it
    const int page = NativePad::instance().page_keys() | NativePad::instance().section_stick();
    in.previous_section = (page & 1) != 0 || pressed(pad, kL2);
    in.next_section = (page & 2) != 0 || pressed(pad, kR2);
    in.seconds = std::clamp(seconds, 0.0f, 0.1f);
    if (mouse && mouse->have && aspect > 0.0f) {   // mac5: the pointer in the page's picture (b3settings' pointer rules)
        const float fw = std::min(1.0f, (4.0f / 3.0f) / aspect), fh = std::min(1.0f, aspect / (4.0f / 3.0f));
        in.pointer = std::array<float, 2>{(mouse->x - 0.5f * (1.0f - fw)) / fw * 640.0f, (mouse->y - 0.5f * (1.0f - fh)) / fh * 448.0f};
        in.pointer_moved = mouse->moved;
        in.click = mouse->click;
        in.click_held = mouse->held;
        in.wheel = mouse->wheel;
        in.back = in.back || mouse->back;
    }
    b3settings::Env env;
#if defined(_WIN32)
    env.graphics_api = true;   // d3d12: the GRAPHICS API row
#endif
    env.hd_pack = (std::getenv("B3_HD_PACK") != nullptr && *std::getenv("B3_HD_PACK")) || b3asset::hd_textures() != nullptr;
    env.hd_pack_loaded = b3asset::hd_textures() != nullptr;
    env.xbox_textures = !b3asset::hd_xbox_folder().empty();   // xboximport: the XBOX TEXTURES row
    bool hd_changed = false;
    {   // mac5: a folder chosen in the dialog (its callback may run on another thread)
        std::lock_guard<std::mutex> lock(save_m_);
        if (hd_pending_) {
            hd_pending_ = false;
            if (settings_.hd_pack_dir != hd_pending_dir_) { settings_.hd_pack_dir = hd_pending_dir_; hd_changed = true; }
        }
        env.hd_pack_status = hd_status_;
    }
    bool music_changed = false;
    {   // a music folder chosen in the dialog
        std::lock_guard<std::mutex> lock(save_m_);
        if (music_pending_) {
            music_pending_ = false;
            if (settings_.music_folder != music_pending_dir_) { settings_.music_folder = music_pending_dir_; music_changed = true; }
        }
        env.music_status = music_status_;
    }
    env.music = true;   // native_play plays it (tools/native/custom_soundtrack.h)
    scan_languages();
    env.languages = languages_;   // the launcher imports them (IMPORT LANGUAGES)
    {
        std::lock_guard<std::mutex> lock(music_m_);
        env.now_playing = now_playing_;   // nowplaying1
    }
    if (save_.info) {   // save1: the SAVES tab
        char folder[1024] = {};
        env.profile = save_.info(folder, int(sizeof folder));
        env.saves_folder = folder;
        if (env.profile > 0) {   // the save files there (save_store.h layout), and the one the next start uses
            namespace fs = std::filesystem;
            std::error_code ec;
            for (std::size_t k = 0; k < env.profile_used.size(); ++k) {
                const std::string n = "profile-" + std::to_string(k + 1);
                env.profile_used[k] = fs::exists(b3io::u8path(folder) / (n + ".sav"), ec) || fs::is_directory(b3io::u8path(folder) / n, ec);
            }
            if (FILE* f = b3io::fopen(b3io::u8path(folder) / "active-slot.txt", "rb")) {
                int v = 0;
                if (std::fscanf(f, "%d", &v) == 1 && v >= 1 && v <= int(env.profile_used.size())) env.next_profile = v;
                std::fclose(f);
            }
        }
        save_profile_ = env.profile;
        save_folder_ = env.saves_folder;
        char text[512] = {};
        const int serial = save_.status ? save_.status(text, int(sizeof text)) : 0;
        std::lock_guard<std::mutex> lock(save_m_);
        if (serial != save_serial_seen_) { save_serial_seen_ = serial; save_local_.clear(); }
        env.save_status = !save_local_.empty() ? save_local_ : serial ? std::string(text) : std::string();
    }
    std::vector<b3mod::SettingRow> mod_rows;   // sdk2b: the MODS tab (a section per mod, then its rows)
    {
        std::lock_guard<std::mutex> lock(mod_m_);
        mod_rows = mod_rows_;
    }
    std::string last_mod;
    for (std::size_t k = 0; k < mod_rows.size(); ++k) {
        const b3mod::SettingRow& m = mod_rows[k];
        auto upper = [](std::string s) {
            for (char& c : s)
                if (c >= 'a' && c <= 'z') c = char(c - 'a' + 'A');
            return s;
        };
        if (m.mod != last_mod) {
            last_mod = m.mod;
            b3settings::Row sec;
            sec.id = b3settings::kSectionModBase + std::uint32_t(k);
            sec.kind = b3settings::RowKind::kSection;
            sec.label = upper(m.mod);
            env.mod_rows.push_back(std::move(sec));
        }
        b3settings::Row r;
        r.id = b3settings::kModRowBase + std::uint32_t(k);
        r.label = upper(m.label);
        r.description = m.description;
        r.value = std::size_t(std::max(m.value, 0));
        if (m.kind == b3mod::SettingKind::kSlider) {
            r.kind = b3settings::RowKind::kSlider;
            r.levels = m.count;
            r.level_text = std::to_string(m.value);
        } else {
            r.kind = m.kind == b3mod::SettingKind::kToggle ? b3settings::RowKind::kToggle : b3settings::RowKind::kChoice;
            r.values = m.values;
        }
        env.mod_rows.push_back(std::move(r));
    }
    const std::uint64_t t_tabs = timing.on ? SDL_GetTicksNS() : 0;
    env.extra_tabs = TuneConsole::instance().tabs(settings_.developer_mods != 0);   // tune4 (tuneconsole.h)
    if (timing.on && !env.extra_tabs.empty()) timing.add(0, ms_since(t_tabs));
    controls_family();                    // padlayout: the layout each player's pad gets (Auto)
    controls_flow(pad, seconds, mouse);   // remap1: a rebind's input, before the rows are built
    controls_env(env);
    portable_scan();
    env.portable = portable_;   // portable mode (1.3.0)
    if (visit_capture_) {   // nextstart: SAVE FILE AT START at the opening (a file: the rows read it)
        visit_capture_ = false;
        visit_next_profile_ = env.next_profile;
    }
    if (ask_open_) return ask_frame(in, env, ini);   // nextstart: the question box takes the input until answered
    b3settings::Content content = b3settings::build_content(settings_, env);
    if (flow_.active()) content.popup = flow_.message(pad_style());   // the menu takes no input while it shows
    // settingsui: the prompt bar follows the last input: the mouse (its words), the keyboard or a controller (glyphs)
    content.prompt_device = MenuMouse::instance().used_last()            ? b3settings::Content::PromptDevice::kMouse
                            : prompt_device_ == PromptDevice::kKeyboard ? b3settings::Content::PromptDevice::kKeyboard
                                                                         : b3settings::Content::PromptDevice::kPad;
    b3settings::translate(content, b3settings::ui_language(settings_, env));   // 1.1: our own text in the player's language
    {   // nextstart: the next-start rows changed since this run's start, marked (SAVE FILE AT START: the one in use then)
        b3settings::Env env_started = env;
        env_started.next_profile = 0;
        b3settings::mark_pending(content, started_, env_started, b3settings::ui_language(settings_, env));
    }
    const b3settings::Frame f = menu_.update(content, in);
    if (timing.on) {
        const double ms = ms_since(t_frame);
        timing.add(1, ms);
        const std::size_t rows = menu_.tab() < content.tabs.size() ? content.tabs[menu_.tab()].rows.size() : 0;
        if (menu_.focus() != timing.last_focus || menu_.tab() != timing.last_tab) {
            if (timing.last_focus != std::size_t(-1))
                std::printf("[b3ui] PC SETTINGS: tab %zu row %zu of %zu (%s%s%s%s), the step %.3f ms\n", menu_.tab(), menu_.focus(), rows,
                            in.up ? "up " : "", in.down ? "down " : "", in.previous_section ? "page up " : "",
                            in.next_section ? "page down " : "", ms);
            timing.last_focus = menu_.focus();
            timing.last_tab = menu_.tab();
        }
        timing.report(menu_.tab(), rows);
    }
    bool changed = false;
    for (const b3settings::Edit& e : f.edits) {
        if (controls_edit(e)) continue;   // remap1
        if (e.id >= b3settings::kModRowBase && e.id < b3settings::kModRowEnd) {   // sdk2b: for native_play (its next VBlank)
            const std::size_t k = e.id - b3settings::kModRowBase;
            std::lock_guard<std::mutex> lock(mod_m_);
            if (k < mod_rows_.size() && e.value < mod_rows_[k].count) {
                mod_rows_[k].value = int(e.value);
                mod_changes_.push_back(mod_rows_[k].qualified() + "=" + b3mod::value_text(mod_rows_[k], int(e.value)));
                ++edits_;
            }
            continue;
        }
        if (TuneConsole::instance().edit(e)) continue;   // tune4: a DEVELOPER row (its edits go to native_play)
        if (e.action && (e.id == b3settings::kActionExportSave || e.id == b3settings::kActionImportSave ||
                         e.id == b3settings::kActionOpenSaveFolder)) {
            save_action(e.id, env);
            continue;
        }
        if (e.action && e.id == b3settings::kActionChooseHdPack) {   // mac5: the system's folder dialog
            if (const char* given = std::getenv("B3_HD_PACK_DIALOG"); given) { hd_folder_done(given, nullptr); continue; }   // scripted
            const std::string start = settings_.hd_pack_dir.empty() ? std::string() : settings_.hd_pack_dir;
            SDL_ShowOpenFolderDialog([](void* user, const char* const* files, int) {
                                         static_cast<PcPage*>(user)->hd_folder_done(files ? files[0] : nullptr, files ? nullptr : SDL_GetError());
                                     },
                                     this, nullptr, start.empty() ? nullptr : start.c_str(), false);
            continue;
        }
        if (e.action && e.id == b3settings::kActionChooseMusic) {   // the system's folder dialog
            if (const char* given = std::getenv("B3_MUSIC_DIALOG"); given) { music_folder_done(given, nullptr); continue; }   // scripted
            const std::string start = settings_.music_folder;
            SDL_ShowOpenFolderDialog([](void* user, const char* const* files, int) {
                                         static_cast<PcPage*>(user)->music_folder_done(files ? files[0] : nullptr, files ? nullptr : SDL_GetError());
                                     },
                                     this, nullptr, start.empty() ? nullptr : start.c_str(), false);
            continue;
        }
        if (e.action && e.id == b3settings::kActionClearMusic) {
            if (!settings_.music_folder.empty()) { settings_.music_folder.clear(); changed = true; ++edits_; }
            std::lock_guard<std::mutex> lock(save_m_);
            music_status_ = "No music folder: the game's EA TRAX songs.";
            continue;
        }
        if (e.action && e.id == b3settings::kActionClearHdPack) {
            if (!settings_.hd_pack_dir.empty()) { settings_.hd_pack_dir.clear(); changed = true; ++edits_; }
            std::lock_guard<std::mutex> lock(save_m_);
            hd_status_ = "No HD pack from the next start.";
            continue;
        }
        if (e.id == b3settings::kRowSaveFile) {   // active-slot.txt (read at the next start)
            const int slot = int(e.value) + 1;
            const std::filesystem::path file = b3io::u8path(env.saves_folder) / "active-slot.txt";
            const std::string path = b3io::u8string(file);
            const bool ok = b3io::replace_file(file, std::to_string(slot) + "\n");   // fsrobust
            set_local_status(ok ? (slot == env.profile ? "SAVE FILE " + std::to_string(slot) + " STAYS IN USE"
                                                       : "SAVE FILE " + std::to_string(slot) + " FROM THE NEXT START")
                                : "CANNOT WRITE " + path);
            continue;
        }
        if (e.id == b3settings::kRowPortable) {   // portable mode (1.3.0): asked of the launcher (it moves the data)
            if (!portable_.shown || !portable_.disabled.empty()) continue;
            const bool want = e.value != 0;
            if (want == portable_.on) {
                b3io::clear_portable_request(b3io::u8path(portable_dir_));
                portable_.request = -1;
            } else if (b3io::write_portable_request(b3io::u8path(portable_dir_), want)) {
                portable_.request = want ? 1 : 0;
            }
            std::printf("[b3settings] PORTABLE MODE: %s\n", portable_.request < 0 ? "as it is" : portable_.request ? "on at the launcher's next start"
                                                                                                          : "off at the launcher's next start");
            continue;
        }
        if (b3settings::language_edit(settings_, env, e)) { changed = true; ++edits_; continue; }   // 1.0.1
        if (b3settings::apply_edit(settings_, e)) { changed = true; ++edits_; }
    }
    if (hd_changed) { changed = true; ++edits_; }
    if (music_changed) { changed = true; ++edits_; }
    if (changed) ++serial_;
    if (f.close && !flow_.active()) {   // nextstart: a next-start row changed in this visit: the question box first
        b3settings::Env env_visit = env;
        env_visit.next_profile = visit_next_profile_;
        std::vector<b3settings::PendingChange> ch = b3settings::next_start_changes(visit_, env_visit, settings_, env);
        if (!ch.empty()) {
            const std::string lang = b3settings::ui_language(settings_, env);
            std::string list;
            for (b3settings::PendingChange& c : ch) {
                list += (list.empty() ? "" : ", ") + c.label + " " + c.before + " > " + c.after;
                c.label = b3settings::translate_text(c.label, lang);
                c.before = b3settings::translate_text(c.before, lang);
                c.after = b3settings::translate_text(c.after, lang);
            }
            ask_changes_ = std::move(ch);
            ask_open_ = true;
            ask_menu_.open(0);
            ask_menu_.set_panel_pointer(true);
            ask_menu_.hold_until_released();   // (the press that closed the page does not answer the box)
            std::printf("[b3settings] PC SETTINGS: settings for the next start changed (%s): asked%s\n", list.c_str(),
                        replay_playback_ ? " (a replay plays: no restart)" : in_race_ ? " (in a race)" : "");
            std::fflush(stdout);
            return changed;
        }
    }
    if (f.close && close_page(ini)) changed = true;
    return changed;
}

bool PcPage::close_page(const std::string& ini) {
    bool changed = false;
    {
        if (flow_.active()) {   // remap1: a rebind left open
            flow_.cancel();
            NativePad::instance().end_capture();
        }
        active_.store(false);
        std::string why;
        if (controls_unsynced_) {   // fsrobust: the edits' controls.ini, flushed to the disk once
            controls_unsynced_ = false;
            if (!b3native::input::save_controls(controls_path_, controls_, &why))
                std::printf("[b3controls] controls.ini NOT SAVED at the page's close: %s\n", why.c_str());
        }
        // settingsui: the file read again and only the page's own changes written (the launcher's edits meanwhile stay)
        const b3settings::Settings before = settings_;
        const bool saved = !ini.empty() && b3settings::save_merged(ini, saved_, settings_, &why);
        if (saved) {
            saved_ = settings_;
            if (!(settings_ == before)) ++serial_, changed = true;   // another program's changes taken in: applied too
        }
        std::printf("[b3settings] PC SETTINGS closed (%llu edits in all), preset %s, %s\n", static_cast<unsigned long long>(edits_),
                    b3settings::preset_name(b3settings::matching_preset(settings_)),
                    saved ? ("saved to " + ini).c_str() : ini.empty() ? "no ini" : why.c_str());
        std::fflush(stdout);
    }
    return changed;
}

// nextstart: the question box (page.h restart_ask_content): KEEP (also Back), APPLY AND RESTART, REVERT
bool PcPage::ask_frame(const b3settings::Input& in, const b3settings::Env& env, const std::string& ini) {
    const std::string lang = b3settings::ui_language(settings_, env);
    b3settings::Content c = b3settings::restart_ask_content(ask_changes_, in_race_, !replay_playback_);
    c.prompt_device = MenuMouse::instance().used_last()            ? b3settings::Content::PromptDevice::kMouse
                      : prompt_device_ == PromptDevice::kKeyboard ? b3settings::Content::PromptDevice::kKeyboard
                                                                   : b3settings::Content::PromptDevice::kPad;
    b3settings::translate(c, lang);
    const b3settings::Frame f = ask_menu_.update(c, in);
    int answer = f.close ? 0 : -1;   // 0 keep, 1 restart, 2 revert
    for (const b3settings::Edit& e : f.edits) {
        if (!e.action) continue;
        if (e.id == b3settings::kActionAskKeep) answer = 0;
        else if (e.id == b3settings::kActionAskRestart && !replay_playback_) answer = 1;
        else if (e.id == b3settings::kActionAskRevert) answer = 2;
    }
    if (answer < 0) return false;
    bool changed = false;
    if (answer == 2) {   // the visit's values back (SAVE FILE AT START: active-slot.txt rewritten as it was)
        b3settings::revert_next_start(settings_, visit_);
        if (env.next_profile != visit_next_profile_ && !env.saves_folder.empty()) {
            const int slot = visit_next_profile_ > 0 ? visit_next_profile_ : env.profile;
            if (slot > 0) b3io::replace_file(b3io::u8path(env.saves_folder) / "active-slot.txt", std::to_string(slot) + "\n");
        }
        changed = true;
        ++edits_;
        ++serial_;
    }
    std::printf("[b3settings] PC SETTINGS: the next-start settings: %s\n",
                answer == 0 ? "kept (applied at the next start)" : answer == 1 ? "applied: the game starts again" : "reverted");
    std::fflush(stdout);
    ask_open_ = false;
    if (close_page(ini)) changed = true;
    if (answer == 1) restart_requested_.store(true);   // (after the ini is saved)
    return changed;
}

// ---- paintsel: the car paint panel on SELECT CAR (pcpage.h panel_open; tools/native/paint_select.h) ----

bool PcPage::save_now(const std::string& ini, std::string& why) {
    if (ini.empty()) { why = "no ini"; return false; }
    if (!b3settings::save_merged(ini, saved_, settings_, &why)) return false;
    saved_ = settings_;
    return true;
}

bool PcPage::paint_step(const std::string& ini) {
    NativePad& np = NativePad::instance();
    bool changed = false;
    if (const int m = paint_game_pending_.exchange(0)) {   // the game's colour changed on SELECT CAR: Game paint again
        for (int p = 0; p < 2; ++p) {
            int& paint = p == 0 ? settings_.car_paint : settings_.p2_car_paint;
            if (!(m >> p & 1)) continue;
            if (paint != 1) {
                std::printf("[b3settings] SELECT CAR: the game's colour changed (player %d): CAR PAINT stays Game\n", p + 1);
            } else {
                paint = 0;
                changed = true;
                ++serial_;
                std::string why;
                const bool saved = !active_.load() && save_now(ini, why);   // (an open panel saves when it closes)
                std::printf("[b3settings] SELECT CAR: the game's colour changed (player %d): %s back to Game (%s)\n", p + 1,
                            p == 0 ? "CAR PAINT" : "P2 CAR PAINT", saved ? ("saved to " + ini).c_str() : active_.load() ? "saved at the close" : why.c_str());
            }
            std::fflush(stdout);
        }
    }
    if (paint_open_.load() && !paint_laid_) {   // opened by the game thread (paint_toggle): the menu, here
        paint_laid_ = true;
        paint_edits_ = 0;
        paint_last_ = b3native::input::neutral_sample();
        paint_layer_ = b3settings::kLayerBody;   // paint2
        paint_picker_ = b3settings::ColourPicker{};
        paint_menu_.open(0);
        std::printf("[b3settings] car paint panel opened (player %d)\n", paint_player_.load() + 1);
        std::fflush(stdout);
    }
    if (paint_open_.load() && np.paint_page() < 0) paint_close(ini, "SELECT CAR gone");
    return changed;
}

int PcPage::paint_toggle(int player) {
    if (paint_open_.load()) {
        paint_close_asked_.store(true);
        return 0;
    }
    if (active_.load()) return -1;   // PC SETTINGS
    paint_player_.store(player == 1 ? 1 : 0);
    paint_close_asked_.store(false);
    NativePad::instance().panel_feed(true, player == 1 ? 1 : 0);
    paint_open_.store(true);
    active_.store(true);
    return 1;
}

void PcPage::paint_close(const std::string& ini, const char* why_closed) {
    if (!paint_open_.load()) return;
    NativePad::instance().panel_feed(false);
    paint_laid_ = false;
    paint_close_asked_.store(false);
    paint_open_.store(false);
    active_.store(false);
    std::string why;
    const bool saved = save_now(ini, why);
    std::printf("[b3settings] car paint panel closed (%s; player %d, %llu edits): CAR PAINT %s, P2 CAR PAINT %s, FINISH %d, %s\n", why_closed,
                paint_player_.load() + 1, static_cast<unsigned long long>(paint_edits_), settings_.car_paint == 1 ? "Custom" : "Game",
                settings_.p2_car_paint == 1 ? "Custom" : "Game", settings_.paint_finish, saved ? ("saved to " + ini).c_str() : why.c_str());
    std::fflush(stdout);
}

bool PcPage::paint_frame(const std::array<std::uint8_t, 32>&, float seconds, const std::string& ini, const MenuMouse::PagePointer* mouse) {
    if (paint_close_asked_.load()) {   // the panel's button again (native_play, at its VBlank)
        paint_close(ini, "the CAR PAINT PANEL button");
        return false;
    }
    // the reports since the last frame, in VBlank order (player 2's panel: player 2's pad and the keyboard, not player
    // 1's pads: input_keys.h panel_report); none new: the last one held
    std::vector<std::array<std::uint8_t, 32>> reports = NativePad::instance().take_panel_reports();
    if (reports.empty()) reports.push_back(paint_last_);
    else paint_last_ = reports.back();
    bool changed = false;
    const float dt = std::clamp(seconds, 0.0f, 0.1f) / float(reports.size());
    for (const std::array<std::uint8_t, 32>& pad : reports) {
        if (paint_step_report(pad, dt, mouse)) changed = true;
        if (!paint_open_.load()) break;
    }
    if (paint_open_.load() && paint_back_) paint_close(ini, "Back");
    paint_back_ = false;
    return changed;
}

bool PcPage::paint_step_report(const std::array<std::uint8_t, 32>& pad, float seconds, const MenuMouse::PagePointer* mouse) {
    if (paint_back_) return false;
    const int lx = int(pad[4]) - 0x80, ly = int(pad[5]) - 0x80;
    const int player = paint_player_.load();
    if (paint_picker_.on()) {   // paint2: the colour picker takes the pad (and the pointer) until CONFIRM or BACK
        b3settings::PickerInput pi;
        pi.left = pressed(pad, kLeft) || lx < -0x40;
        pi.right = pressed(pad, kRight) || lx > 0x40;
        pi.up = pressed(pad, kUp) || ly < -0x40;
        pi.down = pressed(pad, kDown) || ly > 0x40;
        pi.hue_down = pressed(pad, kL1);
        pi.hue_up = pressed(pad, kR1);
        pi.fine = pressed(pad, kSquare);
        pi.accept = pressed(pad, kCross);
        pi.cancel = pressed(pad, kTriangle) || pressed(pad, kStart) || (mouse && mouse->have && mouse->back);
        pi.seconds = seconds;
        if (mouse && mouse->have && paint_aspect_ > 0.0f) {
            const float fw = std::min(1.0f, (4.0f / 3.0f) / paint_aspect_), fh = std::min(1.0f, paint_aspect_ / (4.0f / 3.0f));
            pi.pointer = std::array<float, 2>{(mouse->x - 0.5f * (1.0f - fw)) / fw * 640.0f, (mouse->y - 0.5f * (1.0f - fh)) / fh * 448.0f};
            pi.pointer_held = mouse->held || mouse->click;
        }
        const b3settings::Content shown = b3settings::paint_panel_content(settings_, player, paint_layer_);
        const std::size_t rows = shown.tabs.empty() ? 0 : shown.tabs[0].rows.size();
        const b3settings::Menu::PickerBoxes pb = b3settings::Menu::picker_boxes(b3settings::Menu::panel_detail_y(rows));
        std::copy(std::begin(pb.sq), std::end(pb.sq), pi.sq);
        std::copy(std::begin(pb.bar), std::end(pb.bar), pi.bar);
        const b3settings::ColourPicker::Result r = paint_picker_.step(pi);
        const b3settings::PaintLayerRef L = b3settings::paint_layer(settings_, player, paint_layer_);
        bool changed = false;
        if (r == b3settings::ColourPicker::Result::kChanged) {
            for (int k = 0; k < 3; ++k) *L.rgb[k] = paint_picker_.rgb()[k];
            *L.custom = 1;
            changed = true;
        } else if (r == b3settings::ColourPicker::Result::kCancelled) {   // the colour (and the switch) as before the picker
            for (int k = 0; k < 3; ++k) *L.rgb[k] = paint_picker_.saved()[k];
            *L.custom = paint_saved_custom_;
            changed = true;
        }
        {   // the rows as they now are, the picker drawn over the description (the rows take no input meanwhile)
            b3settings::Content content = b3settings::paint_panel_content(settings_, player, paint_layer_, paint_picker_.view());
            content.prompt_device = prompt_device_ == PromptDevice::kKeyboard ? b3settings::Content::PromptDevice::kKeyboard
                                                                              : b3settings::Content::PromptDevice::kPad;
            b3settings::translate(content, ui_language());
            b3settings::Input idle;
            idle.seconds = seconds;
            paint_menu_.update(content, idle);
        }
        if (r == b3settings::ColourPicker::Result::kAccepted || r == b3settings::ColourPicker::Result::kCancelled) {
            paint_menu_.hold_until_released();   // the press that closed it does not act on the rows
            std::printf("[b3settings] car paint panel: colour picker %s (player %d, layer %d: %02X%02X%02X)\n",
                        r == b3settings::ColourPicker::Result::kAccepted ? "kept" : "cancelled", player + 1, paint_layer_, *L.rgb[0], *L.rgb[1], *L.rgb[2]);
            std::fflush(stdout);
        }
        if (changed) {
            ++edits_;
            ++paint_edits_;
            ++serial_;
        }
        return changed;
    }
    b3settings::Input in;
    in.up = pressed(pad, kUp) || ly < -0x40;
    in.down = pressed(pad, kDown) || ly > 0x40;
    in.left = pressed(pad, kLeft) || lx < -0x40;
    in.right = pressed(pad, kRight) || lx > 0x40;
    in.confirm = pressed(pad, kCross);
    in.back = pressed(pad, kTriangle) || pressed(pad, kStart) || (mouse && mouse->have && mouse->back);
    in.seconds = seconds;
    b3settings::Content content = b3settings::paint_panel_content(settings_, player, paint_layer_);
    content.prompt_device = prompt_device_ == PromptDevice::kKeyboard ? b3settings::Content::PromptDevice::kKeyboard
                                                                      : b3settings::Content::PromptDevice::kPad;
    b3settings::translate(content, ui_language());
    const b3settings::Frame f = paint_menu_.update(content, in);
    bool changed = false;
    for (const b3settings::Edit& e : f.edits) {
        if (e.id == b3settings::kRowPaintLayer && !e.action) {   // paint2: the layer the rows edit (the panel's own)
            paint_layer_ = std::clamp(int(e.value), 0, b3settings::kPaintLayers - 1);
            continue;
        }
        if (e.id == b3settings::kActionPaintPicker && e.action) {   // paint2: the picker, from the layer's colour
            const b3settings::PaintLayerRef L = b3settings::paint_layer(settings_, player, paint_layer_);
            const int rgb[3] = {*L.rgb[0], *L.rgb[1], *L.rgb[2]};
            paint_saved_custom_ = *L.custom;
            paint_picker_.open(rgb);
            std::printf("[b3settings] car paint panel: colour picker opened (player %d, layer %d)\n", player + 1, paint_layer_);
            std::fflush(stdout);
            continue;
        }
        if (b3settings::apply_edit(settings_, e)) {
            changed = true;
            ++edits_;
            ++paint_edits_;
            if (e.id == b3settings::kActionPaintFactory + std::uint32_t(player)) {
                std::printf("[b3settings] car paint panel: factory colours (player %d)\n", player + 1);
                std::fflush(stdout);
            }
        }
    }
    if (changed) ++serial_;
    if (f.close) paint_back_ = true;
    return changed;
}

// ---- remap1: the CONTROLS section (src/native/input_bindings.h) ----

void PcPage::controls_start(const std::string& path) {
    namespace in = b3native::input;
    controls_path_ = path;
    std::vector<std::string> warnings;
    const bool found = !path.empty() && in::load_controls(path, controls_, &warnings);
    if (!found) controls_ = in::default_controls();
    for (const std::string& w : warnings) std::printf("[b3controls] %s\n", w.c_str());
    std::printf("[b3controls] %s: %s (controller layout: player 1 %s, player 2 %s)\n", path.empty() ? "no controls file" : path.c_str(),
                !found ? "the default controls" : controls_ == in::default_controls() ? "the default controls" : "custom controls",
                in::pad_layout_key(controls_.player[0].layout), in::pad_layout_key(controls_.player[1].layout));
    std::fflush(stdout);
    NativePad::instance().set_controls(controls_);
}

std::uint32_t PcPage::port_row_id(b3native::input::PortAction a) {
    using PA = b3native::input::PortAction;
    switch (a) {
    case PA::PhotoMotion: return b3settings::kActionCtlPhotoFirst;
    case PA::PhotoShutterFaster: return b3settings::kActionCtlPhotoFirst + 1;
    case PA::PhotoShutterSlower: return b3settings::kActionCtlPhotoFirst + 2;
    case PA::RaceTime: return b3settings::kActionCtlRaceTime;   // todrace
    case PA::PaintPanel: return b3settings::kActionCtlPaint;    // paintsel
    case PA::ShiftUp: return b3settings::kActionCtlShiftUp;     // gearbox
    case PA::ShiftDown: return b3settings::kActionCtlShiftDown;
    case PA::PartyMoonJump: return b3settings::kActionCtlPartyFirst;       // party1
    case PA::PartyCrash: return b3settings::kActionCtlPartyFirst + 1;
    case PA::PartyAiDriver: return b3settings::kActionCtlPartyFirst + 2;
    case PA::Replay: return b3settings::kActionCtlReplay;           // replayview
    case PA::ReplayCameraPrev: return b3settings::kActionCtlReplayFirst;   // replaycams
    case PA::ReplayCameraNext: return b3settings::kActionCtlReplayFirst + 1;
    case PA::ReplayCarPrev: return b3settings::kActionCtlReplayFirst + 2;
    case PA::ReplayCarNext: return b3settings::kActionCtlReplayFirst + 3;
    case PA::ReplayFreeCamera: return b3settings::kActionCtlReplayFirst + 4;
    default: return 0;
    }
}

std::vector<b3native::input::PortAction> PcPage::port_list(b3native::input::Device d) {
    std::vector<b3native::input::PortAction> o;
    for (int a = 0; a < b3native::input::kPortActions; ++a)
        if (b3native::input::port_listed(b3native::input::PortAction(a)) &&   // 1.3.0: TIME OF DAY PER RACE held back
            (d == b3native::input::Device::Keyboard || b3native::input::port_has_pad(b3native::input::PortAction(a)))) o.push_back(b3native::input::PortAction(a));
    return o;
}

b3native::input::PadStyle PcPage::pad_style() const {
    switch (prompt_device_) {
    case PromptDevice::kPlayStation: case PromptDevice::kPlayStationModern: return b3native::input::PadStyle::PlayStation;
    case PromptDevice::kXbox: return b3native::input::PadStyle::Xbox;
    case PromptDevice::kNintendo: return b3native::input::PadStyle::Nintendo;
    default: return b3native::input::PadStyle::Generic;
    }
}

void PcPage::controls_env(b3settings::Env& env) const {
    namespace in = b3native::input;
    b3settings::Env::Controls& k = env.controls;
    k.on = true;
    k.player = ctl_player_;
    k.device = ctl_device_;
    k.view = ctl_view_;
    k.status = ctl_status_;
    const in::Device dev = ctl_device_ == 0 ? in::Device::Keyboard : in::Device::Gamepad;
    const in::KeyCtx view = ctl_view_ == 0 ? in::KeyCtx::Drive : in::KeyCtx::Menu;   // padlayout: the controller's too
    if (ctl_player_ < 2) {   // padlayout: CONTROLLER LAYOUT, Auto worded for the player's pad
        const int type = NativePad::instance().pad_type(int(ctl_player_));
        k.layout = std::size_t(controls_.player[ctl_player_].layout);
        k.layout_auto = in::auto_text(in::page_family(controls_, int(ctl_player_)), type >= 0);
    }
    if (ctl_player_ >= 2) {   // rebind2: the shortcuts (port actions)
        for (in::PortAction a : port_list(dev)) {
            b3settings::Env::Controls::Action row;
            row.id = port_row_id(a);   // photoblur: the motion blur's in their own block
            row.label = in::port_label(a);
            row.value = in::bound_port_text(controls_, a, dev, pad_style());
            row.description = std::string(in::port_use(a)) + " Bound: " + row.value + ". CONFIRM: press a new " +
                              (dev == in::Device::Keyboard ? "key" : "button, stick or trigger") +
                              " for it; RIGHT: add one more" + (dev == in::Device::Gamepad ? " (held together with the rest)" : "") +
                              "; LEFT: remove the last; SQUARE: back to the default.";
            k.actions.push_back(std::move(row));
        }
        return;
    }
    for (in::Ds2 a : in::action_order()) {
        b3settings::Env::Controls::Action row;
        row.label = in::action_label(a);
        row.value = in::bound_text(controls_, int(ctl_player_), dev, a, view, pad_style());
        const std::string drive = in::action_use(a, true), menus = in::action_use(a, false);
        std::string use;
        if (!drive.empty()) use += "Driving: " + drive + ". ";
        if (!menus.empty()) use += "Menus: " + menus + ". ";
        row.description = use + "Bound: " + row.value + ". CONFIRM: press a new " +
                          (dev == in::Device::Keyboard ? "key" : "button, stick or trigger") +
                          " for it; RIGHT: add one more; LEFT: remove the last; SQUARE: back to the default.";
        k.actions.push_back(std::move(row));
    }
    if (dev == in::Device::Gamepad) {   // pad1: the player's gyro steering
        const in::GyroControls& g = controls_.player[std::min<std::size_t>(ctl_player_, 1)].gyro;
        k.gyro = true;
        k.gyro_mode = g.mode;
        k.gyro_axis = g.axis;
        k.gyro_sensitivity = std::size_t(std::clamp(g.sensitivity, 25, 300) / 25 - 1);
        k.gyro_dead_zone = std::size_t(std::clamp(g.dead_zone, 0, 10));
        const auto& ch = in::gyro_recentre_choices();
        k.gyro_recentre = std::size_t(std::find(ch.begin(), ch.end(), g.recentre) - ch.begin());
        for (in::PadInput p : ch) k.gyro_recentre_labels.push_back(p == in::PadInput::None ? "NONE" : in::pad_input_label(p, pad_style()));
    }
}

void PcPage::controls_family() {
    namespace in = b3native::input;
    for (int p = 0; p < in::kPlayers; ++p) {
        const int type = NativePad::instance().pad_type(p);
        in::set_page_family(controls_, p, type < 0 ? in::PadStyle::Generic : in::pad_family(type));
    }
}

// polish122: the LAST CHANGE row in the page's language: `shown` is `what` with the port's own words translated (the
// key and button names stay); the log keeps the English `what`
void PcPage::controls_changed(const std::string& what, const std::string& shown) {
    namespace in = b3native::input;
    NativePad::instance().set_controls(controls_);
    std::string why;
    // fsrobust: on every edit, replaced safely but not flushed to the disk (the render thread); once when the page closes
    const bool saved = !controls_path_.empty() && in::save_controls(controls_path_, controls_, &why, false);
    controls_unsynced_ = controls_unsynced_ || saved;
    const std::string lang = ui_language();
    const auto tr = [&lang](const std::string& s) { return b3settings::translate_text(s, lang); };
    ctl_status_ = (lang.empty() || shown.empty() ? tr(what) : shown) +   // English: `what` as before
                  (saved ? "" : controls_path_.empty() ? " " + tr("(this run only)")
                                                       : " " + b3settings::translate_format("(NOT SAVED: {})", why, lang));
    std::printf("[b3controls] %s; %s\n", what.c_str(),
                saved ? ("saved to " + controls_path_).c_str() : controls_path_.empty() ? "no controls file: this run only" : why.c_str());
    std::fflush(stdout);
}

bool PcPage::controls_edit(const b3settings::Edit& e) {
    namespace in = b3native::input;
    const in::Device dev = ctl_device_ == 0 ? in::Device::Keyboard : in::Device::Gamepad;
    const in::KeyCtx view = ctl_view_ == 0 ? in::KeyCtx::Drive : in::KeyCtx::Menu;   // padlayout: the controller's too
    const bool port = ctl_player_ >= 2;   // rebind2: the shortcuts
    const std::string who = port ? std::string("SHORTCUTS ") + (dev == in::Device::Keyboard ? "KEYBOARD " : "CONTROLLER ")
                                 : "PLAYER " + std::to_string(ctl_player_ + 1) + (dev == in::Device::Keyboard ? " KEYBOARD" : " CONTROLLER") +
                                       (view == in::KeyCtx::Drive ? " (DRIVING) " : " (MENUS) ");
    // polish122: the same in the page's language (LAST CHANGE)
    const std::string lang = ui_language();
    const auto tr = [&lang](const std::string& s) { return b3settings::translate_text(s, lang); };
    const std::string player_tr = tr("PLAYER") + " " + std::to_string(ctl_player_ + 1);
    const std::string who_tr = port ? tr("SHORTCUTS") + " " + tr(dev == in::Device::Keyboard ? "KEYBOARD" : "CONTROLLER") + " "
                                    : player_tr + " " + tr(dev == in::Device::Keyboard ? "KEYBOARD" : "CONTROLLER") + " (" +
                                          tr(view == in::KeyCtx::Drive ? "DRIVING" : "MENUS") + ") ";
    if (e.id != b3settings::kActionCtlResetAll) reset_all_armed_ = false;
    if (e.id == b3settings::kRowCtlPlayer) { ctl_player_ = std::min<std::size_t>(e.value, 2); return true; }
    if (e.id == b3settings::kRowCtlDevice) { ctl_device_ = std::min<std::size_t>(e.value, 1); return true; }
    if (e.id == b3settings::kRowCtlView) { ctl_view_ = std::min<std::size_t>(e.value, 1); return true; }
    if (e.id == b3settings::kRowCtlLayout && !port) {   // padlayout: the player's CONTROLLER LAYOUT (Custom keeps its bindings)
        const in::PadLayout l = in::PadLayout(std::min<std::size_t>(e.value, in::kPadLayouts - 1));
        if (l != controls_.player[ctl_player_].layout) {
            in::set_layout(controls_, int(ctl_player_), l);
            const int type = NativePad::instance().pad_type(int(ctl_player_));
            const std::string lt = in::layout_text(controls_.player[ctl_player_], in::page_family(controls_, int(ctl_player_)), type >= 0);
            controls_changed("PLAYER " + std::to_string(ctl_player_ + 1) + " CONTROLLER LAYOUT " + lt,
                             player_tr + " " + tr("CONTROLLER LAYOUT") + " " + tr(lt));
        }
        return true;
    }
    if (e.id >= b3settings::kRowCtlGyroMode && e.id <= b3settings::kRowCtlGyroRecentre) {   // pad1: gyro steering
        in::GyroControls& g = controls_.player[std::min<std::size_t>(ctl_player_, 1)].gyro;
        const in::GyroControls before = g;
        const std::size_t v = e.value;
        const auto& ch = in::gyro_recentre_choices();
        static const char* const kModes[] = {"OFF", "MIX", "REPLACE"};
        static const char* const kModeValues[] = {"Off", "Mix", "Replace"};   // the row's values (page.cpp)
        std::string what, shown;
        switch (e.id) {
        case b3settings::kRowCtlGyroMode:
            g.mode = std::uint8_t(std::min<std::size_t>(v, 2));
            what = std::string("GYRO STEERING ") + kModes[g.mode];
            shown = tr("GYRO STEERING") + " " + tr(kModeValues[g.mode]);
            break;
        case b3settings::kRowCtlGyroAxis:
            g.axis = std::uint8_t(std::min<std::size_t>(v, 1));
            what = g.axis ? "GYRO AXIS TURN" : "GYRO AXIS TILT";
            shown = tr("GYRO AXIS") + " " + tr(g.axis ? "Turn" : "Tilt");
            break;
        case b3settings::kRowCtlGyroSensitivity:
            g.sensitivity = int(std::min<std::size_t>(v, 11) + 1) * 25;
            what = "GYRO SENSITIVITY " + std::to_string(g.sensitivity) + "%";
            shown = tr("GYRO SENSITIVITY") + " " + std::to_string(g.sensitivity) + "%";
            break;
        case b3settings::kRowCtlGyroDeadZone:
            g.dead_zone = int(std::min<std::size_t>(v, 10));
            what = "GYRO DEAD ZONE " + std::to_string(g.dead_zone) + " DEG";
            shown = tr("GYRO DEAD ZONE") + " " + b3settings::translate_format("{} DEG", std::to_string(g.dead_zone), lang);
            break;
        default:
            g.recentre = ch[std::min(v, ch.size() - 1)];
            what = "GYRO RECENTRE " + (g.recentre == in::PadInput::None ? std::string("NONE") : in::pad_input_label(g.recentre, pad_style()));
            shown = tr("GYRO RECENTRE") + " " + (g.recentre == in::PadInput::None ? std::string("NONE") : in::pad_input_label(g.recentre, pad_style()));
            break;
        }
        if (!(g == before)) controls_changed("PLAYER " + std::to_string(ctl_player_ + 1) + " " + what, player_tr + " " + shown);
        return true;
    }
    if (e.id == b3settings::kActionCtlReset && e.action) {
        if (e.direction != 0) return true;   // left / right on the button: nothing
        if (port) {
            for (in::PortAction pa : port_list(dev)) in::reset_port(controls_, pa, dev);
        } else {
            in::reset(controls_, int(ctl_player_), dev);
        }
        controls_changed(who + "RESET TO DEFAULTS", who_tr + tr("RESET TO DEFAULTS"));
        return true;
    }
    if (e.id == b3settings::kActionCtlResetAll && e.action) {   // asks first: a second press does it
        if (e.direction != 0) return true;
        if (!reset_all_armed_) {
            reset_all_armed_ = true;
            ctl_status_ = "PRESS RESET ALL CONTROLS AGAIN TO PUT EVERYTHING BACK (ANY OTHER ROW CANCELS)";
            return true;
        }
        reset_all_armed_ = false;
        in::reset_all(controls_);
        controls_changed("ALL CONTROLS RESET TO DEFAULTS", tr("ALL CONTROLS RESET TO DEFAULTS"));
        return true;
    }
    if (!b3settings::is_ctl_action(e.id) || !e.action) return false;
    const std::size_t k = e.id - b3settings::kActionCtlFirst;
    if (port) {   // rebind2: a shortcut or a photo mode control (photoblur: its row id as controls_env gives it)
        const std::vector<in::PortAction> list = port_list(dev);
        std::size_t at = list.size();
        for (std::uint32_t i = 0, next = b3settings::kActionCtlFirst; i < list.size() && at == list.size(); ++i) {
            const std::uint32_t own = port_row_id(list[i]);
            if ((own ? own : next++) == e.id) at = i;
        }
        if (at >= list.size()) return true;
        const in::PortAction pa = list[at];
        if (e.direction == 2) {
            const in::Controls before = controls_;
            in::reset_port(controls_, pa, dev);
            if (!(controls_ == before)) {
                const std::string b = in::bound_port_text(controls_, pa, dev, pad_style());
                controls_changed(who + in::port_label(pa) + ": " + b, who_tr + tr(in::port_label(pa)) + ": " + b);
            }
            return true;
        }
        if (e.direction < 0) {
            const in::Controls before = controls_;
            in::remove_last_port(controls_, pa, dev);
            if (!(controls_ == before)) {
                const std::string b = in::bound_port_text(controls_, pa, dev, pad_style());
                controls_changed(who + in::port_label(pa) + ": " + b, who_tr + tr(in::port_label(pa)) + ": " + b);
            }
            return true;
        }
        flow_.begin_port(pa, dev, e.direction > 0);
        NativePad::instance().begin_capture(dev);
        flow_left_ = flow_right_ = flow_confirm_ = flow_back_ = true;
        std::printf("[b3controls] rebind: %s%s%s\n", who.c_str(), in::port_label(pa), e.direction > 0 ? " (add)" : "");
        std::fflush(stdout);
        return true;
    }
    if (k >= in::action_order().size()) return true;
    const in::Ds2 a = in::action_order()[k];
    if (e.direction == 2) {   // SQUARE: this one back to its default
        const in::Controls before = controls_;
        in::reset_action(controls_, int(ctl_player_), dev, a, view);
        if (!(controls_ == before)) {
            const std::string b = in::bound_text(controls_, int(ctl_player_), dev, a, view, pad_style());
            controls_changed(who + in::action_label(a) + ": " + b, who_tr + tr(in::action_label(a)) + ": " + b);
        }
        return true;
    }
    if (e.direction < 0) {   // LEFT: the last input shown goes
        const in::Controls before = controls_;
        if (in::remove_last(controls_, int(ctl_player_), dev, a, view) == in::Removed::kKeptForMenus) {   // settingsui
            ctl_status_ = tr(in::kKeptForMenusText);
            ++serial_;
            std::printf("[b3controls] %s%s: %s\n", who.c_str(), in::action_label(a), in::kKeptForMenusText);
            std::fflush(stdout);
            return true;
        }
        if (!(controls_ == before)) {
            const std::string b = in::bound_text(controls_, int(ctl_player_), dev, a, view, pad_style());
            controls_changed(who + in::action_label(a) + ": " + b, who_tr + tr(in::action_label(a)) + ": " + b);
        }
        return true;
    }
    flow_.begin(int(ctl_player_), dev, a, view, e.direction > 0);
    NativePad::instance().begin_capture(dev);
    flow_left_ = flow_right_ = flow_confirm_ = flow_back_ = true;   // the press that opened it is not an answer
    std::printf("[b3controls] rebind: %s%s%s\n", who.c_str(), in::action_label(a), e.direction > 0 ? " (add)" : "");
    std::fflush(stdout);
    return true;
}

void PcPage::controls_flow(const std::array<std::uint8_t, 32>& pad, float seconds, const MenuMouse::PagePointer* mouse) {
    namespace in = b3native::input;
    if (!flow_.active()) return;
    using S = in::RebindFlow::State;
    const in::Ds2 a = flow_.action();
    const auto finish = [&]() {
        NativePad::instance().end_capture();
        menu_.hold_until_released();   // settingsui: the input just taken (still held) does not also act on the page
        const std::string lang = ui_language();   // polish122: LAST CHANGE in the page's language
        const auto tr = [&lang](const std::string& s) { return b3settings::translate_text(s, lang); };
        if (flow_.state() == S::Done && flow_.is_port()) {
            const std::string b = in::bound_port_text(controls_, flow_.port_action(), flow_.device(), pad_style());
            controls_changed(std::string("SHORTCUTS ") + in::port_label(flow_.port_action()) + ": " + b,
                             tr("SHORTCUTS") + " " + tr(in::port_label(flow_.port_action())) + ": " + b);
        } else if (flow_.state() == S::Done) {
            const std::string b = in::bound_text(controls_, flow_.player(), flow_.device(), a, flow_.view(), pad_style());
            controls_changed("PLAYER " + std::to_string(flow_.player() + 1) + " " + in::action_label(a) + ": " + b,
                             tr("PLAYER") + " " + std::to_string(flow_.player() + 1) + " " + tr(in::action_label(a)) + ": " + b);
        } else {
            std::printf("[b3controls] rebind cancelled\n");
            std::fflush(stdout);
        }
    };
    if (flow_.state() == S::Waiting) {
        NativePad::Capture c;
        if (NativePad::instance().take_capture(c)) {
            if (c.cancel) flow_.cancel();
            else flow_.input(controls_, c.in);
            if (flow_.state() == S::Conflict) {
                NativePad::instance().end_capture();   // the answer comes from the page's own input
                flow_left_ = flow_right_ = flow_confirm_ = flow_back_ = true;
                std::printf("[b3controls] %s\n", flow_.message(pad_style()).c_str());
                std::fflush(stdout);
            }
        } else {
            flow_.tick(std::clamp(seconds, 0.0f, 0.1f));
        }
        if (!flow_.active()) finish();
        return;
    }
    // the conflict: LEFT / RIGHT choose, CONFIRM (or a click) applies, BACK cancels -- on fresh presses
    const int lx = int(pad[4]) - 0x80;
    const bool left = pressed(pad, kLeft) || lx < -0x40, right = pressed(pad, kRight) || lx > 0x40;
    const bool confirm = pressed(pad, kCross) || (mouse && mouse->click);
    const bool back = pressed(pad, kTriangle) || pressed(pad, kStart) || (mouse && mouse->back);
    if (left && !flow_left_) flow_.step(-1);
    if (right && !flow_right_) flow_.step(1);
    if (back && !flow_back_) flow_.cancel();
    else if (confirm && !flow_confirm_) flow_.choose(controls_);
    flow_left_ = left;
    flow_right_ = right;
    flow_confirm_ = confirm;
    flow_back_ = back;
    if (!flow_.active()) finish();
}

// ---- save1: export / import through the system's file dialog (SDL3); $B3NATIVE_SAVE_DIALOG=<path> answers it
// (scripted checks). Without a dialog (none on this system): export to <saves>/exports, import the first file in
// <saves>/import.
namespace {
struct DialogCall { PcPage* page; int kind; };
void SDLCALL dialog_cb(void* user, const char* const* files, int) {
    auto* c = static_cast<DialogCall*>(user);
    if (!files) c->page->dialog_done(c->kind, nullptr, SDL_GetError());
    else c->page->dialog_done(c->kind, files[0], nullptr);
    delete c;
}
std::string stamp() {
    const std::time_t t = std::time(nullptr);
    char b[32] = "export";
    if (const std::tm* tm = std::localtime(&t)) std::strftime(b, sizeof b, "%Y%m%d-%H%M%S", tm);
    return b;
}
}  // namespace

void PcPage::set_local_status(std::string s) {
    std::printf("[b3settings] SAVES: %s\n", s.c_str());
    std::fflush(stdout);
    std::lock_guard<std::mutex> lock(save_m_);
    save_local_ = std::move(s);
}

void PcPage::save_action(std::uint32_t id, const b3settings::Env& env) {
    if (!save_.request || env.profile <= 0) return;
    if (id == b3settings::kActionOpenSaveFolder) {
        if (const char* given = std::getenv("B3NATIVE_SAVE_DIALOG"); given && *given) {   // scripted: no window opened
            set_local_status("OPEN FOLDER " + env.saves_folder);
            return;
        }
        const std::string url = b3port::file_url(env.saves_folder);   // robust2: percent-encoded ("Application Support")
        // (the run log names every window the game asks the system to open: issue #3's reports of file manager windows)
        std::printf("[b3settings] SAVES: SAVE FOLDER > Open: asking the system to show %s\n", url.c_str());
        std::fflush(stdout);
        if (!SDL_OpenURL(url.c_str())) set_local_status(std::string("CANNOT OPEN THE FOLDER: ") + SDL_GetError());
        return;
    }
    const int kind = id == b3settings::kActionExportSave ? 1 : 2;
    if (const char* given = std::getenv("B3NATIVE_SAVE_DIALOG"); given && *given) {
        dialog_done(kind, given, nullptr);
        return;
    }
    static const SDL_DialogFileFilter kExport[] = {{"Burnout 3 port save", "sav"}};
    static const SDL_DialogFileFilter kImport[] = {{"Saves (.sav, .psu, .max, .ps2)", "sav;psu;max;ps2;mcd;bin"},
                                                   {"All files", "*"}};
    const std::string def = (std::filesystem::path(env.saves_folder) / ("Burnout3-profile-" + std::to_string(env.profile) + ".sav")).string();
    if (kind == 1) SDL_ShowSaveFileDialog(dialog_cb, new DialogCall{this, kind}, nullptr, kExport, 1, def.c_str());
    else {   // 1.1: the dialog starts in PCSX2's memory card folder when there is one (pcsx2_cards.h), else the save folder
        b3native::save::Pcsx2Homes h;
        const char* home = SDL_GetUserFolder(SDL_FOLDER_HOME);
        const char* docs = SDL_GetUserFolder(SDL_FOLDER_DOCUMENTS);
        auto from_utf8 = [](const char* t) { const std::string u(t); return std::filesystem::path(std::u8string(u.begin(), u.end())); };
        if (home) h.home = from_utf8(home);
        if (docs) h.documents = from_utf8(docs);
        if (const char* x = std::getenv("XDG_CONFIG_HOME"); x && *x) h.xdg_config = std::filesystem::path(x);
#ifdef _WIN32
        for (const char* v : {"ProgramFiles", "ProgramFiles(x86)"})
            if (const char* p = std::getenv(v); p && *p) h.program_files.push_back(std::filesystem::path(p));
#endif
        std::string start = env.saves_folder;
        if (const auto f = b3native::save::pcsx2_card_folders(h); !f.empty()) {
            const std::u8string u = (f.front().path / "").u8string();
            start.assign(u.begin(), u.end());
        }
        SDL_ShowOpenFileDialog(dialog_cb, new DialogCall{this, kind}, nullptr, kImport, 2, start.c_str(), false);
    }
}

std::string PcPage::save_alert() {
    if (!save_.status) return {};
    char text[512] = {};
    const int serial = save_.status(text, int(sizeof text));
    if (serial == alert_serial_seen_) return {};
    alert_serial_seen_ = serial;
    // (save_store.h ProfileCard kSaveFailed; saveflow: kNotSaved, kSaveWarning, kSaved and fsrobust's "SAVED AGAIN ...")
    for (const char* p : {"SAVE FAILED", "NOT SAVED", "SAVE WARNING", "SAVED"})
        if (std::strncmp(text, p, std::strlen(p)) == 0) return std::string(text);
    return {};
}

void PcPage::dialog_done(int kind, const char* path, const char* error) {
    namespace fs = std::filesystem;
    if (path && *path) {
        save_.request(kind, path);
        set_local_status(kind == 1 ? "EXPORTING..." : "IMPORTING...");
        return;
    }
    if (!error) { set_local_status("NO FILE CHOSEN"); return; }   // cancelled
    // no dialog on this system: the save folder's own export / import folders
    std::error_code ec;
    const fs::path folder(save_folder_);
    if (kind == 1) {
        fs::create_directories(folder / "exports", ec);
        const fs::path out = folder / "exports" / ("profile-" + std::to_string(save_profile_) + "-" + stamp() + ".sav");
        save_.request(kind, out.string().c_str());
        set_local_status("EXPORTING...");
        return;
    }
    std::vector<fs::path> files;
    for (const auto& e : fs::directory_iterator(folder / "import", ec)) files.push_back(e.path());
    std::sort(files.begin(), files.end());
    if (files.empty()) {
        fs::create_directories(folder / "import", ec);
        set_local_status("NO FILE DIALOG (" + std::string(error) + "): PUT THE SAVE IN " + (folder / "import").string() +
                         " AND CHOOSE IMPORT AGAIN");
        return;
    }
    save_.request(kind, files.front().string().c_str());
    set_local_status("IMPORTING " + files.front().filename().string() + "...");
}

void PcPage::portable_scan() {
    if (portable_known_) return;
    portable_known_ = true;
    namespace fs = std::filesystem;
    const b3io::UserDirCase c = b3io::this_user_dir_case();
    const b3io::UserDirChoice in_use = b3io::resolve_user_dir(c);
    // the player's own data folder only (an offscreen run, a test, an explicit folder: no row)
    if (controls_path_.empty() || in_use.kind == b3io::UserDirKind::kOverride || in_use.dir.empty() ||
        !b3io::portable_supported(c.apple, c.appimage, c.flatpak) || c.exe_dir.empty())
        return;
    std::error_code ec;
    if (fs::weakly_canonical(b3io::u8path(controls_path_).parent_path(), ec) != fs::weakly_canonical(in_use.dir, ec)) return;
    portable_.shown = true;
    portable_.on = in_use.kind == b3io::UserDirKind::kPortable;
    portable_dir_ = b3io::u8string(in_use.dir);
    if (const auto r = b3io::read_portable_request(in_use.dir)) portable_.request = *r == portable_.on ? -1 : int(*r);
    if (!b3io::dir_writable(c.exe_dir))
        portable_.disabled = "The install folder cannot be written by you: move the game to a folder of your own to use portable mode.";
}

void PcPage::scan_languages() {
    if (languages_scanned_) return;   // 1.0.1: once (a disc scan per frame would cost the frame)
    languages_scanned_ = true;
    // 1.1: the folder native_play reads the game's text from (B3NATIVE_LANGUAGES_DIR, set by native_play for every
    // run); else <prefs>/languages beside the controls file (a window run without native_play's word)
    std::filesystem::path root;
    if (const auto d = b3io::getenv("B3NATIVE_LANGUAGES_DIR")) root = b3io::u8path(*d);   // fsrobust: the wide value
    else if (!controls_path_.empty()) root = b3asset::language::imported_root(std::filesystem::path(controls_path_).parent_path());
    for (const auto& l : b3asset::language::on_disc(root))
        if (l.supported) languages_.emplace_back(l.code, l.name);
}

std::string PcPage::ui_language() {
    scan_languages();
    b3settings::Env env;
    env.languages = languages_;
    return b3settings::ui_language(settings_, env);
}

// mac5: the HD pack folder: a PCSX2 replacement folder has image files (dds / png) somewhere below it; the renderer's
// index decides at the next start (b3asset::hd_enable)
void PcPage::hd_folder_done(const char* path, const char* error) {
    std::lock_guard<std::mutex> lock(save_m_);
    if (!path || !*path) {
        hd_status_ = error && *error ? std::string("No folder dialog on this system (") + error + "): use the launcher or --hd-pack."
                                     : std::string("No folder chosen.");
        return;
    }
    namespace fs = std::filesystem;
    std::error_code ec;
    bool images = false;
    std::size_t seen = 0;
    for (fs::recursive_directory_iterator it(path, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end && seen < 200000;
         it.increment(ec), ++seen) {
        std::string ext = it->path().extension().string();
        for (char& c : ext) c = char(std::tolower(static_cast<unsigned char>(c)));
        if (ext == ".dds" || ext == ".png") { images = true; break; }
    }
    hd_pending_ = true;
    hd_pending_dir_ = path;
    hd_status_ = images ? "Folder chosen: in use from the next start." : "Chosen, but no .dds / .png images were found in it.";
    std::printf("[b3settings] HD pack folder %s (%s)\n", path, images ? "images found" : "no images");
    std::fflush(stdout);
}

void PcPage::music_folder_done(const char* path, const char* error) {
    std::lock_guard<std::mutex> lock(save_m_);
    if (!path || !*path) {
        music_status_ = error && *error ? std::string("No folder dialog on this system (") + error + ")." : std::string("No folder chosen.");
        return;
    }
    namespace fs = std::filesystem;
    std::error_code ec;
    std::size_t songs = 0, seen = 0;
    for (fs::recursive_directory_iterator it(fs::path(std::u8string(path, path + std::strlen(path))), fs::directory_options::skip_permission_denied, ec), end;
         !ec && it != end && seen < 200000; it.increment(ec), ++seen) {
        std::string ext = it->path().extension().string();
        for (char& c : ext) c = char(std::tolower(static_cast<unsigned char>(c)));
        if (ext == ".mp3" || ext == ".flac" || ext == ".ogg" || ext == ".oga" || ext == ".wav") ++songs;
    }
    music_pending_ = true;
    music_pending_dir_ = path;
    music_status_ = songs ? std::to_string(songs) + " songs found: in use from the next song (PLAYLIST: Custom only or Mixed)."
                          : "Chosen, but no MP3, FLAC, Ogg Vorbis or WAV files were found in it.";
    std::printf("[b3settings] music folder %s (%zu songs)\n", path, songs);
    std::fflush(stdout);
}

// the custom soundtrack's values for the game's thread (b3r_music): at each frame, and as the settings are loaded
// (extrastart: the game asks from its first VBlank)
void PcPage::publish_music() {
    std::lock_guard<std::mutex> lock(music_m_);
    if (music_folder_pub_ != settings_.music_folder) music_folder_pub_ = settings_.music_folder;
    playlist_mode_pub_ = settings_.playlist_mode;
    music_volume_pub_ = settings_.custom_music_volume;
    music_loudness_pub_ = settings_.custom_music_loudness;   // musicloud
    sound_output_pub_ = settings_.sound_output;   // audioq
}

int PcPage::music_volume() {
    std::lock_guard<std::mutex> lock(music_m_);
    return music_volume_pub_;
}

int PcPage::music_loudness() {   // musicloud
    std::lock_guard<std::mutex> lock(music_m_);
    return music_loudness_pub_;
}

int PcPage::sound_output() {
    std::lock_guard<std::mutex> lock(music_m_);
    return sound_output_pub_;
}

int PcPage::music(char* folder, int n) {
    std::lock_guard<std::mutex> lock(music_m_);
    if (folder && n > 0) {
        const std::size_t k = std::min(music_folder_pub_.size(), std::size_t(n - 1));
        std::memcpy(folder, music_folder_pub_.data(), k);
        folder[k] = 0;
    }
    return playlist_mode_pub_;
}

// ---- drawing the page's items (include/b3settings/page.h: the item vocabulary) with the game's captured fonts

// The face a text item asks for, by its font texture's name; a font whose texture is not captured yet: the first one.
const HostFont* PcPage::font_for(std::uint8_t flags, const HostFonts& fonts,
                                 const std::map<std::uint64_t, std::shared_ptr<const HudTexture>>& textures) const {
    const char* want = (flags & b3settings::kHeadingFont) ? "HeadFont"
                     : (flags & b3settings::kTextSizeMask) == b3settings::kTextSmall ? "SmallFont" : "GlobalFont";
    const HostFont* first = nullptr;
    for (const HostFont& f : fonts.fonts) {
        if (f.glyphs.empty()) continue;
        if (!first) first = &f;
        for (const auto& [key, t] : textures)
            if (t && t->raster == f.raster && t->name == want) return &f;
    }
    return first;
}

namespace {

std::uint32_t with_alpha(std::uint32_t c, float scale) {
    const float a = std::clamp(float(c >> 24) * scale, 0.0f, 255.0f);
    return (c & 0x00FFFFFFu) | (std::uint32_t(std::lround(a)) << 24);
}
std::uint32_t darker(std::uint32_t c, float f) {
    std::uint32_t o = c & 0xFF000000u;
    for (int k = 0; k < 3; ++k) o |= std::uint32_t(std::lround(float((c >> (8 * k)) & 0xFF) * f)) << (8 * k);
    return o;
}

}  // namespace


// ---- settingsui: the prompt bar's glyphs ----

PromptKeys PcPage::page_prompt_keys() const {
    PromptKeys k = prompt_keys_;
    if (prompt_device_ == PromptDevice::kKeyboard) {   // the section keys (the shortcuts SECTION PREV / NEXT: Page Up / Down)
        const auto& prev = controls_.port.keys[std::size_t(b3native::input::PortAction::SectionPrev)];
        const auto& next = controls_.port.keys[std::size_t(b3native::input::PortAction::SectionNext)];
        k.scancode[std::size_t(PadButton::kL2)] = prev.empty() ? 0 : prev.front();
        k.scancode[std::size_t(PadButton::kR2)] = next.empty() ? 0 : next.front();
    }
    return k;
}

void PcPage::build_glyphs() {
    namespace g = glyph;
    const PromptKeys keys = page_prompt_keys();
    if (glyph_image_ && glyph_device_ == prompt_device_ && glyph_keys_ == keys) return;
    glyph_device_ = prompt_device_;
    glyph_keys_ = keys;
    const PromptDevice d = prompt_device_;
    const bool kb = d == PromptDevice::kKeyboard;
    constexpr float H = 64;   // pixels (drawn 12-16 picture units high: about 30-40 pixels at 1080p)
    struct Want { std::string name; int button; float w; };   // button: a PadButton, or -1..-4 the D-pad / arrows
    std::vector<Want> want = {{"cross", int(PadButton::kCross), 0},   {"circle", int(PadButton::kCircle), 0},
                              {"square", int(PadButton::kSquare), 0}, {"triangle", int(PadButton::kTriangle), 0},
                              {"l1", int(PadButton::kL1), 0},         {"r1", int(PadButton::kR1), 0},
                              {"l2", int(PadButton::kL2), 0},         {"r2", int(PadButton::kR2), 0},
                              {"l3", int(PadButton::kL3), 0},         {"r3", int(PadButton::kR3), 0},
                              {"start", int(PadButton::kStart), 0},   {"select", int(PadButton::kSelect), 0},
                              {"dpad-ud", -1, H}, {"dpad-lr", -2, H}, {"dpad-l", -3, H}, {"dpad-r", -4, H}};
    // the section keys: the keyboard's (L2 / R2 here: Page Up / Down), a controller's right stick (R3's glyph, once)
    want.push_back({"section-up", kb ? int(PadButton::kL2) : int(PadButton::kR3), 0});
    if (kb) want.push_back({"section-down", int(PadButton::kR2), 0});
    float total = 0;
    for (Want& w : want) {
        if (w.button >= 0) {
            g::Canvas probe(1, 1);
            w.w = paint_prompt_glyph(probe, 0, 0, H, d, PadButton(w.button), keys);
        }
        total += std::ceil(w.w) + 4;
    }
    g::Canvas c(static_cast<int>(total), static_cast<int>(H));
    const float on[4] = {1, 1, 1, 1}, dim[4] = {0.55f, 0.62f, 0.75f, 0.85f}, cap[4] = {0.10f, 0.16f, 0.30f, 0.9f};
    const auto arrow = [&](float cx, float cy, float r, int dir, const float (&col)[4]) {   // 0 up, 1 down, 2 left, 3 right
        std::vector<g::Pt> t;
        if (dir == 0) t = {{cx, cy - r}, {cx + r, cy + r * 0.6f}, {cx - r, cy + r * 0.6f}};
        if (dir == 1) t = {{cx, cy + r}, {cx - r, cy - r * 0.6f}, {cx + r, cy - r * 0.6f}};
        if (dir == 2) t = {{cx - r, cy}, {cx + r * 0.6f, cy - r}, {cx + r * 0.6f, cy + r}};
        if (dir == 3) t = {{cx + r, cy}, {cx - r * 0.6f, cy + r}, {cx - r * 0.6f, cy - r}};
        c.polygon(t, col);
    };
    glyph_cells_.clear();
    float x = 0;
    for (const Want& w : want) {
        const float cx = x + w.w * 0.5f, cy = H * 0.5f;
        if (w.button >= 0) {
            paint_prompt_glyph(c, x, 0, H, d, PadButton(w.button), keys);
        } else if (kb) {   // an arrow key cap with its arrows
            c.box(cx, cy, H * 0.44f, H * 0.44f, H * 0.12f, cap);
            c.box_outline(cx, cy, H * 0.44f, H * 0.44f, H * 0.12f, H * 0.05f, on);
            if (w.button == -1) { arrow(cx, cy - H * 0.17f, H * 0.13f, 0, on); arrow(cx, cy + H * 0.17f, H * 0.13f, 1, on); }
            if (w.button == -2) { arrow(cx - H * 0.17f, cy, H * 0.13f, 2, on); arrow(cx + H * 0.17f, cy, H * 0.13f, 3, on); }
            if (w.button == -3) arrow(cx, cy, H * 0.18f, 2, on);
            if (w.button == -4) arrow(cx, cy, H * 0.18f, 3, on);
        } else {   // a D-pad: its cross, the arms that act bright
            const float a = H * 0.46f, t = H * 0.16f;
            c.box(cx, cy, t, a, H * 0.05f, dim);
            c.box(cx, cy, a, t, H * 0.05f, dim);
            const bool up = w.button == -1, side = w.button == -2;
            if (up) { c.box(cx, cy - a * 0.55f, t, a * 0.45f, H * 0.05f, on); c.box(cx, cy + a * 0.55f, t, a * 0.45f, H * 0.05f, on); }
            if (side || w.button == -3) c.box(cx - a * 0.55f, cy, a * 0.45f, t, H * 0.05f, on);
            if (side || w.button == -4) c.box(cx + a * 0.55f, cy, a * 0.45f, t, H * 0.05f, on);
        }
        glyph_cells_[w.name] = GlyphCell{x / float(c.w), 0, (x + w.w) / float(c.w), 1, w.w / H};
        x += std::ceil(w.w) + 4;
    }
    auto im = std::make_shared<HostUi::Image>();
    im->name = "pcglyphs-" + std::to_string(++glyph_serial_);
    im->w = c.w;
    im->h = c.h;
    im->rgba.resize(std::size_t(c.w) * std::size_t(c.h) * 4);
    c.store(im->rgba.data(), c.w, 0, 0);
    glyph_image_ = std::move(im);
}

std::shared_ptr<const HostUi> PcPage::ui(const std::shared_ptr<const HostFonts>& fonts,
                                         const std::map<std::uint64_t, std::shared_ptr<const HudTexture>>& textures) {
    using namespace b3settings;
    if (!active_.load()) return nullptr;
    if (!fonts || fonts->fonts.empty()) {
        if (!warned_) {
            warned_ = true;
            std::printf("[b3settings] PC SETTINGS: the game's fonts are not captured yet: the page is not drawn\n");
        }
        return nullptr;
    }
    if (built_fonts_ != fonts->serial) {   // the fonts the page uses, once per capture
        built_fonts_ = fonts->serial;
        for (const HostFont& f : fonts->fonts) {
            std::string name = "(texture not captured yet)";
            for (const auto& [key, t] : textures)
                if (t && t->raster == f.raster) { name = t->name; break; }
            std::printf("[b3settings] PC SETTINGS font %08x %s: %zu glyphs, cell %.4f x %.4f\n", f.raster, name.c_str(), f.glyphs.size(),
                        double(f.scale_x), double(f.scale_y));
        }
    }
    const std::uint64_t t_ui = ui_timing().on ? SDL_GetTicksNS() : 0;
    auto ui = std::make_shared<HostUi>();
    ui->serial = std::uint32_t(++built_serial_);
    ui->paths.push_back(std::string());   // path 0: plain colour
    build_glyphs();   // settingsui: the prompt bar's glyphs, for the device in use
    std::uint32_t glyph_path = 0;
    if (glyph_image_) {
        ui->images.push_back(glyph_image_);
        glyph_path = std::uint32_t(ui->paths.size());
        ui->paths.push_back("img:" + glyph_image_->name);
    }
    std::map<std::uint32_t, std::uint32_t> path_index;   // font raster -> path
    const auto path_of = [&](std::uint32_t raster) {
        auto it = path_index.find(raster);
        if (it == path_index.end()) {
            char name[32];
            std::snprintf(name, sizeof name, "hudtex:%08x", raster);
            it = path_index.emplace(raster, std::uint32_t(ui->paths.size())).first;
            ui->paths.push_back(name);
        }
        return it->second;
    };
    const auto texture_of = [&](std::uint32_t raster) -> const HudTexture* {
        for (const auto& [key, t] : textures)
            if (t && t->raster == raster && t->ok) return t.get();
        return nullptr;
    };
    const auto add = [&](const float (&x)[4], const float (&y)[4], const float (&u)[4], const float (&v)[4], std::uint32_t c,
                         std::uint32_t path, std::uint8_t cover = 0) {
        HostUiQuad q;
        q.cover = cover;
        for (int k = 0; k < 4; ++k) { q.x[k] = x[k]; q.y[k] = y[k]; q.u[k] = u[k]; q.v[k] = v[k]; }
        q.rgba = c;
        q.path = path;
        ui->quads.push_back(q);
    };
    const auto flat = [&](const float (&x)[4], const float (&y)[4], std::uint32_t c, std::uint8_t cover = 0) {
        const float z[4] = {0, 0, 0, 0};
        add(x, y, z, z, c, 0, cover);
    };
    // the size argument that makes a font's line (its tallest glyph cell) `h` picture units tall
    const auto size_for = [](const HostFont& f, float h) {
        float line = 0;
        for (const HostFontGlyph& g : f.glyphs) line = std::max(line, g.oy + g.h);
        return line > 0 && f.scale_y > 0 ? h / (line * f.scale_y) : 0.0f;
    };
    const auto widen = [](std::uint8_t flags) { return (flags & kTextSizeMask) == kItalic ? kItalicWidth : 1.0f; };
    const auto measure = [&](std::string_view text, float h, std::uint8_t flags) {
        if (flags & kGlyph) {   // settingsui: a button glyph's width at that height (none: no room)
            const auto it = glyph_cells_.find(std::string(text));
            return it == glyph_cells_.end() || !glyph_path ? 0.0f : it->second.aspect * h;
        }
        const HostFont* f = font_for(flags, *fonts, textures);
        if (!f) return fallback_measure(text, h, flags);
        return f->width(fit_to_font(*f, to_u16(text)), size_for(*f, h)) * widen(flags);
    };
    // One line of glyphs from `x` (the first glyph's left edge), clipped to [clip0, clip1) in x; with `slant`, every
    // point moves right by slant x its height above `bottom`.
    const auto glyph_run = [&](const HostFont& f, const HudTexture& t, std::u16string_view s, float x, float y, float size,
                               float stretch, float clip0, float clip1, float slant, float bottom, std::uint32_t c) {
        if (s.empty() || t.w <= 0 || t.h <= 0) return;
        const float sx = f.scale_x * size * stretch, sy = f.scale_y * size;
        const float nu = float(t.tw) / float(t.w), nv = float(t.th) / float(t.h);   // texel coordinates -> the decoded image
        const std::uint32_t path = path_of(f.raster);
        const HostFontGlyph* g = f.find(s[0]);
        float pen = x - g->ox * sx;
        for (std::size_t k = 0; k < s.size(); ++k) {
            if (k > 0) {
                pen += g->advance * sx;
                g = f.find(s[k]);
            }
            if (g->u < 0.0f || g->w <= 0.0f) continue;   // a space
            float x0 = pen + g->ox * sx, x1 = x0 + g->w * sx;
            float u0 = g->u, u1 = g->u + g->w;
            if (x1 <= clip0 || x0 >= clip1) continue;
            if (x0 < clip0) { u0 += (clip0 - x0) / sx; x0 = clip0; }
            if (x1 > clip1) { u1 -= (x1 - clip1) / sx; x1 = clip1; }
            const float y0 = y + g->oy * sy, y1 = y0 + g->h * sy;
            const float lean0 = slant * (bottom - y0), lean1 = slant * (bottom - y1);
            const float qx[4] = {x0 + lean0, x1 + lean0, x1 + lean1, x0 + lean1}, qy[4] = {y0, y0, y1, y1};
            const float qu[4] = {u0 * nu, u1 * nu, u1 * nu, u0 * nu}, qv[4] = {g->v * nv, g->v * nv, (g->v + g->h) * nv, (g->v + g->h) * nv};
            add(qx, qy, qu, qv, c, path);
        }
    };
    // A text in its box: aligned, clipped to the box, the menu face slanted and widened, a soft drop shadow unless
    // kNoOutline.
    const auto text = [&](std::string_view str, float bx, float by, float bw, float bh, std::uint8_t flags, std::uint32_t c) {
        const HostFont* f = font_for(flags, *fonts, textures);
        const HudTexture* t = f ? texture_of(f->raster) : nullptr;
        if (!f || !t || str.empty() || bh <= 0) return;
        const std::u16string s = fit_to_font(*f, to_u16(str));
        const float size = size_for(*f, bh), stretch = widen(flags);
        const float w = f->width(s, size) * stretch;
        float x = bx;
        if (w < bw && (flags & kAlignRight)) x = bx + bw - w;
        else if (w < bw && (flags & kAlignCentre)) x = bx + 0.5f * (bw - w);
        const float slant = (flags & kTextSizeMask) == kItalic ? kItalicSlant : 0.0f, bottom = by + bh;
        if (!(flags & kNoOutline)) {
            const float d = std::max(0.75f, bh * 0.07f);
            glyph_run(*f, *t, s, x + d, by + d, size, stretch, bx + d, bx + bw + d, slant, bottom + d, with_alpha(0xB4000000u, float(c >> 24) / 255.0f));
        }
        glyph_run(*f, *t, s, x, by, size, stretch, bx, bx + bw, slant, bottom, c);
    };
    for (const Item& it : ask_open_           ? ask_menu_.panel_items("", "", measure)     // nextstart: the question box
                          : paint_open_.load() ? paint_menu_.panel_items("", "", measure)   // paintsel
                                               : menu_.items("", "", measure)) {
        switch (it.kind) {
        case ItemKind::kRect: {
            if (it.w <= 0 || it.h <= 0) break;
            // a parallelogram inside its box (the bottom edge from x, the top edge to x + w), in bands when shaded
            const float lean = (it.flags & kRectSlant) ? kItalicSlant * it.h : 0.0f;
            const int bands = (it.flags & kRectShade) ? std::clamp(int(it.h / 3.0f), 2, 8) : 1;
            const float bottom = it.y + it.h;
            // widescreen: a rect that spans the picture (the backdrop, its wash, a dialog's shade, the grid's lines) is drawn out to
            // the window's edges; the grid's lines repeat sideways (the picture's 640-unit period) so the sides carry the grid too
            constexpr float kPicW = 640.0f, kPicH = 448.0f;
            const std::uint8_t cover = std::uint8_t((it.x <= 0 && it.w >= kPicW ? 1 : 0) | (it.y <= 0 && it.h >= kPicH ? 2 : 0));
            if (cover == 2 && it.w <= 1.5f && lean == 0) {
                for (int j : {-2, -1, 1, 2}) {
                    const float x = it.x + kPicW * float(j);
                    const float qx[4] = {x, x + it.w, x + it.w, x}, qy[4] = {it.y, it.y, it.y + it.h, it.y + it.h};
                    flat(qx, qy, it.colour, cover);
                }
            }
            for (int k = 0; k < bands; ++k) {
                const float y0 = it.y + it.h * float(k) / float(bands), y1 = it.y + it.h * float(k + 1) / float(bands);
                const float l0 = lean > 0 ? kItalicSlant * (bottom - y0) : 0.0f, l1 = lean > 0 ? kItalicSlant * (bottom - y1) : 0.0f;
                const float qx[4] = {it.x + l0, it.x + it.w - lean + l0, it.x + it.w - lean + l1, it.x + l1}, qy[4] = {y0, y0, y1, y1};
                const float f = bands > 1 ? 1.0f - 0.38f * (float(k) + 0.5f) / float(bands) : 1.0f;
                flat(qx, qy, bands > 1 ? darker(it.colour, f) : it.colour, cover);
            }
            break;
        }
        case ItemKind::kImage: {
            const std::string_view n = it.text;
            const auto named = [&](const char* name) {
                const std::string_view s(name);
                return n.size() >= s.size() && n.substr(n.size() - s.size()) == s;
            };
            const float x0 = it.x, x1 = it.x + it.w, y0 = it.y, y1 = it.y + it.h, mx = 0.5f * (x0 + x1), my = 0.5f * (y0 + y1);
            if (named(kImageArrowLeft)) {
                const float qx[4] = {x1, x1, x0, x0}, qy[4] = {y0, y1, my, my};
                flat(qx, qy, it.colour);
            } else if (named(kImageArrowRight)) {
                const float qx[4] = {x0, x1, x1, x0}, qy[4] = {y0, my, my, y1};
                flat(qx, qy, it.colour);
            } else if (named(kImageArrowUp)) {
                const float qx[4] = {mx, mx, x1, x0}, qy[4] = {y0, y0, y1, y1};
                flat(qx, qy, it.colour);
            } else if (named(kImageArrowDown)) {
                const float qx[4] = {x0, x1, mx, mx}, qy[4] = {y0, y0, y1, y1};
                flat(qx, qy, it.colour);
            } else if (n.rfind(kGlyphPrefix, 0) == 0 || ((named(kImagePromptL1) || named(kImagePromptR1)) && glyph_path &&
                                                           glyph_cells_.count(named(kImagePromptL1) ? "l1" : "r1"))) {
                // settingsui: a button glyph (the prompt bar; the tab bar's L1 / R1 too), fitted in its box
                const std::string key = n.rfind(kGlyphPrefix, 0) == 0 ? std::string(n.substr(std::strlen(kGlyphPrefix)))
                                                                    : named(kImagePromptL1) ? "l1" : "r1";
                const auto cell = glyph_cells_.find(key);
                if (cell == glyph_cells_.end() || !glyph_path) break;
                const GlyphCell& gc = cell->second;
                float gh = it.h, gw = gc.aspect * gh;
                if (gw > it.w && gw > 0) { gh *= it.w / gw; gw = it.w; }
                const float gx = x0 + 0.5f * (it.w - gw), gy = y0 + 0.5f * (it.h - gh);
                const float qx[4] = {gx, gx + gw, gx + gw, gx}, qy[4] = {gy, gy, gy + gh, gy + gh};
                const float qu[4] = {gc.u0, gc.u1, gc.u1, gc.u0}, qv[4] = {gc.v0, gc.v0, gc.v1, gc.v1};
                add(qx, qy, qu, qv, it.colour, glyph_path);
            } else if (named(kImagePromptL1) || named(kImagePromptR1)) {
                // the tab prompts: the prompt device's own L1 / R1 label (LB / RB, L / R, the bound keys), fitted in the box
                const std::string label = prompt_label(prompt_device_, named(kImagePromptL1) ? PadButton::kL1 : PadButton::kR1, prompt_keys_);
                float h = it.h;
                const float w = measure(label, h, kTextRegular);
                if (w > it.w && w > 0) h = std::max(it.h * 0.5f, h * it.w / w);
                text(label, it.x, it.y + 0.5f * (it.h - h), it.w, h, kTextRegular | kAlignCentre, it.colour);
            }
            break;
        }
        case ItemKind::kText:
            text(it.text, it.x, it.y, it.w, it.h, it.flags, it.colour);
            break;
        case ItemKind::kFontDirectory:
            break;
        }
    }
    built_ = ui;
    if (ui_timing().on) ui_timing().add(2, ms_since(t_ui));
    return ui;
}

}  // namespace b3replay
