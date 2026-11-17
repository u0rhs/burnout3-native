// pcpage.h -- the PC SETTINGS page in the native renderer (mac5, 2026-11-09; docs/pc-settings.md).
//
// native_play's SETTINGS rows (main menu and race pause menu, tools/native/pc_menu.h) ask the renderer to open the page
// (b3r_settings_open); while it is open the game sees a neutral pad and the page reads the window's (NativePad::last).
// The page model, rows, edits and ini are src/b3settings (the one model the launcher's page uses too); this class only
// runs it here: input from the DS2 report, the items laid out as host-UI quads in the game's own 2D fonts (the capture's
// 'HFN1' record + the fonts' captured HUD textures), edits applied at once, the ini saved when the page closes.
#ifndef B3REPLAY_PCPAGE_H
#define B3REPLAY_PCPAGE_H

#include "b3settings/page.h"
#include "b3settings/settings.h"
#include "../native/input_bindings.h"
#include "hud.h"
#include "menumouse.h"
#include "prompts.h"
#include "b3mod/codemod.h"   // sdk2b: the code mods' settings rows (MODS tab)

#include <array>
#include <deque>
#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>

namespace b3replay {

// save1: the native game's save card (native_play, b3r_set_save_hooks): the SAVES tab's rows and its export / import.
//   info(folder, n)        the active profile (0: no card this run) and the saves folder
//   request(kind, path)    1 export the profile to `path`, 2 import `path` into it (run on the game's thread)
//   status(text, n)        the serial of the last request's result (0: none yet) and its text
struct SaveHooks {
    int (*info)(char* folder, int n) = nullptr;
    void (*request)(int kind, const char* path) = nullptr;
    int (*status)(char* text, int n) = nullptr;
};

class PcPage {
public:
    static PcPage& instance();
    void set_save_hooks(const SaveHooks& h) { save_ = h; }
    // A file dialog's answer (any thread): the path chosen, or none (error: no dialog on this system; else cancelled).
    void dialog_done(int kind, const char* path, const char* error);
    // mac5: the HD pack folder dialog's answer (any thread): applied at the next frame
    void hd_folder_done(const char* path, const char* error);
    // the custom soundtrack's folder dialog's answer (any thread): applied at the next frame
    void music_folder_done(const char* path, const char* error);
    // Any thread (native_play's game thread, b3r_music): the playlist mode and the music folder as the page last had
    // them (published at each frame)
    int music(char* folder, int n);
    // nowplaying1: what OTHER MUSIC APPS detects (any thread: native_play's game thread), shown in its description
    void set_now_playing(const std::string& line) {
        std::lock_guard<std::mutex> lock(music_m_);
        now_playing_ = line;
    }

    // Any thread: open (from b3r_settings_open); true while open.
    void request_open() { open_requested_.store(true); }
    bool active() const { return active_.load(); }

    // Render thread, once per frame: opens on request, runs the model on `pad` (a DS2 report), returns true when the
    // settings changed (the caller applies `settings()`). Saves `ini` when the page closes.
    // mac5: `mouse` (optional) the window's pointer this frame (MenuMouse::page_pointer: fractions of an output of
    // `aspect`, mapped to the page's 640 x 448 picture, the 4:3 box the page is drawn in); a right click is Back.
    bool frame(const std::array<std::uint8_t, 32>& pad, float seconds, const std::string& ini,
               const MenuMouse::PagePointer* mouse = nullptr, float aspect = 4.0f / 3.0f);
    b3settings::Settings& settings() { return settings_; }
    // mac5: the tab buttons' labels follow the prompts' device (L1 / R1, LB / RB, L / R, the bound keys)
    void set_prompts(PromptDevice d, const PromptKeys& keys) { prompt_device_ = d; prompt_keys_ = keys; }
    // Offscreen shots: the page's animation clock in game time (b3settings::Menu::set_clock)
    void set_clock(float seconds) { menu_.set_clock(seconds); }
    // sdk2b, any thread (native_play through b3r_mod_*): the code mods' settings rows (b3mod::rows_to_text; the MODS
    // tab), a value changed by the game side (a script, a replay), and the page's changes for the game, one at a time
    // ("<mod>.<key>=<value>"; 0: none), which native_play applies at its next VBlank
    void set_mod_rows(const std::string& text);
    void set_mod_value(const std::string& qualified, const std::string& value);
    int take_mod_change(char* out, int n);
    // remap1 (src/native/input_bindings.h): the control bindings from controls.ini at `path` (empty or no file: the
    // defaults; an empty path saves nothing), handed to NativePad. INTERFACE > CONTROLS edits them: each change is
    // handed on and saved at once.
    void controls_start(const std::string& path);

    // The page as host-UI quads (hud.h HostUi; font texture paths "hudtex:<raster hex>"), for View::host_ui_list.
    // Null while closed or before the fonts are captured.
    std::shared_ptr<const HostUi> ui(const std::shared_ptr<const HostFonts>& fonts,
                                     const std::map<std::uint64_t, std::shared_ptr<const HudTexture>>& textures);

private:
    void save_action(std::uint32_t id, const b3settings::Env& env);
    // remap1: the CONTROLS section (frame(): the rows' texts, their edits, the rebind's popup and its input)
    void controls_env(b3settings::Env& env) const;
    bool controls_edit(const b3settings::Edit& e);
    void controls_flow(const std::array<std::uint8_t, 32>& pad, float seconds, const MenuMouse::PagePointer* mouse);
    void controls_changed(const std::string& what);
    // rebind2: the shortcuts the page lists for a device (the keyboard: all; a controller: the ones that take a button)
    static std::vector<b3native::input::PortAction> port_list(b3native::input::Device d);
    bool reset_all_armed_ = false;   // RESET ALL CONTROLS asks first
    b3native::input::PadStyle pad_style() const;
    void set_local_status(std::string s);

    const HostFont* font_for(std::uint8_t flags, const HostFonts& fonts,
                             const std::map<std::uint64_t, std::shared_ptr<const HudTexture>>& textures) const;

    std::atomic<bool> open_requested_{false}, active_{false};
    b3settings::Settings settings_;
    b3settings::Menu menu_;
    std::uint64_t edits_ = 0, serial_ = 0, built_serial_ = ~0ull;
    std::shared_ptr<const HostUi> built_;
    std::uint32_t built_fonts_ = 0;
    bool warned_ = false;
    PromptDevice prompt_device_ = PromptDevice::kPlayStation;
    PromptKeys prompt_keys_{};
    SaveHooks save_;
    std::mutex save_m_;
    std::string save_local_;     // a dialog's own result (no file chosen, no dialog), until the next request's result
    int save_serial_seen_ = 0;
    int save_profile_ = 0;
    std::string save_folder_;
    bool hd_pending_ = false;    // mac5: a folder chosen (hd_pending_dir_), to apply on the page's thread
    std::string hd_pending_dir_, hd_status_;
    bool music_pending_ = false;  // the music folder chosen (music_pending_dir_), to apply on the page's thread
    std::string music_pending_dir_, music_status_;
    std::mutex music_m_;         // the published music settings (music_folder_pub_, playlist_mode_pub_)
    // remap1 (render thread)
    std::string controls_path_;
    b3native::input::Controls controls_ = b3native::input::default_controls();
    std::size_t ctl_player_ = 0, ctl_device_ = 0, ctl_view_ = 0;
    b3native::input::RebindFlow flow_;
    bool flow_left_ = true, flow_right_ = true, flow_confirm_ = true, flow_back_ = true;   // the conflict's edges
    std::string ctl_status_;
    std::string music_folder_pub_;
    std::string now_playing_;   // nowplaying1 (music_m_)
    int playlist_mode_pub_ = 0;
    std::mutex mod_m_;            // sdk2b: the code mods' rows and the changes made here
    std::vector<b3mod::SettingRow> mod_rows_;
    std::deque<std::string> mod_changes_;
};

}  // namespace b3replay

#endif
