// pcpage.cpp -- the PC SETTINGS page in the native renderer (pcpage.h).
#include "pcpage.h"

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
#include "b3asset/hdpack.h"
#include "../native/pcsx2_cards.h"   // 1.1: IMPORT SAVE starts in PCSX2's memory cards
#include "tuneconsole.h"   // tune4: the DEVELOPER tab
#include "nativepad.h"

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
    {   // the custom soundtrack's values for the game's thread (b3r_music)
        std::lock_guard<std::mutex> lock(music_m_);
        if (music_folder_pub_ != settings_.music_folder) music_folder_pub_ = settings_.music_folder;
        playlist_mode_pub_ = settings_.playlist_mode;
    }
    if (open_requested_.exchange(false) && !active_.load()) {
        menu_.open(menu_.tab());
        active_.store(true);
        std::printf("[b3settings] PC SETTINGS opened\n");
        std::fflush(stdout);
    }
    if (!active_.load()) return false;
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
    const int page = NativePad::instance().page_keys();
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
    env.hd_pack = (std::getenv("B3_HD_PACK") != nullptr && *std::getenv("B3_HD_PACK")) || b3asset::hd_textures() != nullptr;
    env.hd_pack_loaded = b3asset::hd_textures() != nullptr;
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
    if (!languages_scanned_) {   // 1.0.1: once (a disc scan per frame would cost the frame)
        languages_scanned_ = true;
        // 1.1: the folder native_play reads the game's text from (B3NATIVE_LANGUAGES_DIR, set by native_play for every
        // run); else <prefs>/languages beside the controls file (a window run without native_play's word)
        std::filesystem::path root;
        if (const char* d = std::getenv("B3NATIVE_LANGUAGES_DIR"); d && *d) root = b3mod::path_from_utf8(d);
        else if (!controls_path_.empty()) root = b3asset::language::imported_root(std::filesystem::path(controls_path_).parent_path());
        for (const auto& l : b3asset::language::on_disc(root))
            if (l.supported) languages_.emplace_back(l.code, l.name);
    }
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
                env.profile_used[k] = fs::exists(fs::path(folder) / (n + ".sav"), ec) || fs::is_directory(fs::path(folder) / n, ec);
            }
            if (FILE* f = std::fopen((fs::path(folder) / "active-slot.txt").string().c_str(), "rb")) {
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
    controls_flow(pad, seconds, mouse);   // remap1: a rebind's input, before the rows are built
    controls_env(env);
    b3settings::Content content = b3settings::build_content(settings_, env);
    if (flow_.active()) content.popup = flow_.message(pad_style());   // the menu takes no input while it shows
    b3settings::translate(content, b3settings::ui_language(settings_, env));   // 1.1: our own text in the player's language
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
            const std::string path = (std::filesystem::path(env.saves_folder) / "active-slot.txt").string();
            FILE* f = std::fopen(path.c_str(), "wb");
            const bool ok = f && std::fprintf(f, "%d\n", slot) > 0;
            if (f) std::fclose(f);
            set_local_status(ok ? (slot == env.profile ? "SAVE FILE " + std::to_string(slot) + " STAYS IN USE"
                                                       : "SAVE FILE " + std::to_string(slot) + " FROM THE NEXT START")
                                : "CANNOT WRITE " + path);
            continue;
        }
        if (b3settings::language_edit(settings_, env, e)) { changed = true; ++edits_; continue; }   // 1.0.1
        if (b3settings::apply_edit(settings_, e)) { changed = true; ++edits_; }
    }
    if (hd_changed) { changed = true; ++edits_; }
    if (music_changed) { changed = true; ++edits_; }
    if (changed) ++serial_;
    if (f.close) {
        if (flow_.active()) {   // remap1: a rebind left open
            flow_.cancel();
            NativePad::instance().end_capture();
        }
        active_.store(false);
        std::string why;
        const bool saved = !ini.empty() && b3settings::save_file(ini, settings_, &why);
        std::printf("[b3settings] PC SETTINGS closed (%llu edits in all), preset %s, %s\n", static_cast<unsigned long long>(edits_),
                    b3settings::preset_name(b3settings::matching_preset(settings_)),
                    saved ? ("saved to " + ini).c_str() : ini.empty() ? "no ini" : why.c_str());
        std::fflush(stdout);
    }
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
    std::printf("[b3controls] %s: %s\n", path.empty() ? "no controls file" : path.c_str(),
                !found ? "the default controls" : controls_ == in::default_controls() ? "the default controls" : "custom controls");
    std::fflush(stdout);
    NativePad::instance().set_controls(controls_);
}

std::vector<b3native::input::PortAction> PcPage::port_list(b3native::input::Device d) {
    std::vector<b3native::input::PortAction> o;
    for (int a = 0; a < b3native::input::kPortActions; ++a)
        if (d == b3native::input::Device::Keyboard || b3native::input::port_has_pad(b3native::input::PortAction(a))) o.push_back(b3native::input::PortAction(a));
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
    const in::KeyCtx view = dev == in::Device::Gamepad ? in::KeyCtx::Any : ctl_view_ == 0 ? in::KeyCtx::Drive : in::KeyCtx::Menu;
    if (ctl_player_ >= 2) {   // rebind2: the shortcuts (port actions)
        for (in::PortAction a : port_list(dev)) {
            b3settings::Env::Controls::Action row;
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

void PcPage::controls_changed(const std::string& what) {
    namespace in = b3native::input;
    NativePad::instance().set_controls(controls_);
    std::string why;
    const bool saved = !controls_path_.empty() && in::save_controls(controls_path_, controls_, &why);
    ctl_status_ = what + (saved ? "" : controls_path_.empty() ? " (this run only)" : " (NOT SAVED: " + why + ")");
    std::printf("[b3controls] %s; %s\n", what.c_str(),
                saved ? ("saved to " + controls_path_).c_str() : controls_path_.empty() ? "no controls file: this run only" : why.c_str());
    std::fflush(stdout);
}

bool PcPage::controls_edit(const b3settings::Edit& e) {
    namespace in = b3native::input;
    const in::Device dev = ctl_device_ == 0 ? in::Device::Keyboard : in::Device::Gamepad;
    const in::KeyCtx view = dev == in::Device::Gamepad ? in::KeyCtx::Any : ctl_view_ == 0 ? in::KeyCtx::Drive : in::KeyCtx::Menu;
    const bool port = ctl_player_ >= 2;   // rebind2: the shortcuts
    const std::string who = port ? std::string("SHORTCUTS ") + (dev == in::Device::Keyboard ? "KEYBOARD " : "CONTROLLER ")
                                 : "PLAYER " + std::to_string(ctl_player_ + 1) +
                                       (dev == in::Device::Keyboard ? (view == in::KeyCtx::Drive ? " KEYBOARD (DRIVING) " : " KEYBOARD (MENUS) ") : " CONTROLLER ");
    if (e.id != b3settings::kActionCtlResetAll) reset_all_armed_ = false;
    if (e.id == b3settings::kRowCtlPlayer) { ctl_player_ = std::min<std::size_t>(e.value, 2); return true; }
    if (e.id == b3settings::kRowCtlDevice) { ctl_device_ = std::min<std::size_t>(e.value, 1); return true; }
    if (e.id == b3settings::kRowCtlView) { ctl_view_ = std::min<std::size_t>(e.value, 1); return true; }
    if (e.id >= b3settings::kRowCtlGyroMode && e.id <= b3settings::kRowCtlGyroRecentre) {   // pad1: gyro steering
        in::GyroControls& g = controls_.player[std::min<std::size_t>(ctl_player_, 1)].gyro;
        const in::GyroControls before = g;
        const std::size_t v = e.value;
        const auto& ch = in::gyro_recentre_choices();
        static const char* const kModes[] = {"OFF", "MIX", "REPLACE"};
        std::string what;
        switch (e.id) {
        case b3settings::kRowCtlGyroMode: g.mode = std::uint8_t(std::min<std::size_t>(v, 2)); what = std::string("GYRO STEERING ") + kModes[g.mode]; break;
        case b3settings::kRowCtlGyroAxis: g.axis = std::uint8_t(std::min<std::size_t>(v, 1)); what = g.axis ? "GYRO AXIS TURN" : "GYRO AXIS TILT"; break;
        case b3settings::kRowCtlGyroSensitivity: g.sensitivity = int(std::min<std::size_t>(v, 11) + 1) * 25; what = "GYRO SENSITIVITY " + std::to_string(g.sensitivity) + "%"; break;
        case b3settings::kRowCtlGyroDeadZone: g.dead_zone = int(std::min<std::size_t>(v, 10)); what = "GYRO DEAD ZONE " + std::to_string(g.dead_zone) + " DEG"; break;
        default:
            g.recentre = ch[std::min(v, ch.size() - 1)];
            what = "GYRO RECENTRE " + (g.recentre == in::PadInput::None ? std::string("NONE") : in::pad_input_label(g.recentre, pad_style()));
            break;
        }
        if (!(g == before)) controls_changed("PLAYER " + std::to_string(ctl_player_ + 1) + " " + what);
        return true;
    }
    if (e.id == b3settings::kActionCtlReset && e.action) {
        if (e.direction != 0) return true;   // left / right on the button: nothing
        if (port) {
            for (in::PortAction pa : port_list(dev)) in::reset_port(controls_, pa, dev);
        } else {
            in::reset(controls_, int(ctl_player_), dev);
        }
        controls_changed(who + "RESET TO DEFAULTS");
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
        controls_changed("ALL CONTROLS RESET TO DEFAULTS");
        return true;
    }
    if (e.id < b3settings::kActionCtlFirst || e.id > b3settings::kActionCtlLast || !e.action) return false;
    const std::size_t k = e.id - b3settings::kActionCtlFirst;
    if (port) {   // rebind2: a shortcut or a photo mode control
        const std::vector<in::PortAction> list = port_list(dev);
        if (k >= list.size()) return true;
        const in::PortAction pa = list[k];
        if (e.direction == 2) {
            const in::Controls before = controls_;
            in::reset_port(controls_, pa, dev);
            if (!(controls_ == before)) controls_changed(who + in::port_label(pa) + ": " + in::bound_port_text(controls_, pa, dev, pad_style()));
            return true;
        }
        if (e.direction < 0) {
            const in::Controls before = controls_;
            in::remove_last_port(controls_, pa, dev);
            if (!(controls_ == before)) controls_changed(who + in::port_label(pa) + ": " + in::bound_port_text(controls_, pa, dev, pad_style()));
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
        if (!(controls_ == before))
            controls_changed(who + in::action_label(a) + ": " + in::bound_text(controls_, int(ctl_player_), dev, a, view, pad_style()));
        return true;
    }
    if (e.direction < 0) {   // LEFT: the last input shown goes
        const in::Controls before = controls_;
        in::remove_last(controls_, int(ctl_player_), dev, a, view);
        if (!(controls_ == before))
            controls_changed(who + in::action_label(a) + ": " + in::bound_text(controls_, int(ctl_player_), dev, a, view, pad_style()));
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
        if (flow_.state() == S::Done && flow_.is_port()) {
            controls_changed(std::string("SHORTCUTS ") + in::port_label(flow_.port_action()) + ": " +
                             in::bound_port_text(controls_, flow_.port_action(), flow_.device(), pad_style()));
        } else if (flow_.state() == S::Done) {
            controls_changed("PLAYER " + std::to_string(flow_.player() + 1) + " " + in::action_label(a) + ": " +
                             in::bound_text(controls_, flow_.player(), flow_.device(), a, flow_.view(), pad_style()));
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
        std::string url = "file://";
#ifdef _WIN32
        url += "/";
#endif
        url += env.saves_folder;
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
        const HostFont* f = font_for(flags, *fonts, textures);
        if (!f) return fallback_measure(text, h, flags);
        return f->width(to_u16(text), size_for(*f, h)) * widen(flags);
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
        const std::u16string s = to_u16(str);
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
    for (const Item& it : menu_.items("", "", measure)) {
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
