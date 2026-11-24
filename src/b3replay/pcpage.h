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

// Our own UTF-8 text as the game's font `f` draws it (its 16-bit codes; a Latin-1 letter the font lacks as its base
// letter), as the PC SETTINGS page lays it out (todrace: the track page's label too)
std::u16string host_font_text(const HostFont& f, std::string_view utf8);

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
    // fsrobust: the window's thread, once a frame: a failed save's status text (native_play's card: it begins
    // "SAVE FAILED", save_store.h ProfileCard::kSaveFailed) the first time it is seen, for the notice; else empty.
    // saveflow: also "NOT SAVED", "SAVE WARNING" and "SAVED" (each a small bubble in the window's corner)
    std::string save_alert();
    // A file dialog's answer (any thread): the path chosen, or none (error: no dialog on this system; else cancelled).
    void dialog_done(int kind, const char* path, const char* error);
    // mac5: the HD pack folder dialog's answer (any thread): applied at the next frame
    void hd_folder_done(const char* path, const char* error);
    // the custom soundtrack's folder dialog's answer (any thread): applied at the next frame
    void music_folder_done(const char* path, const char* error);
    // Any thread (native_play's game thread, b3r_music): the playlist mode and the music folder as the page last had
    // them (published at each frame)
    int music(char* folder, int n);
    int music_volume();   // musicvol1: CUSTOM MUSIC VOLUME in percent, as the page last had it (any thread, b3r_music_volume)
    int music_loudness();   // musicloud: CUSTOM MUSIC LOUDNESS (0 Off, 1 Even out), as the page last had it (any thread)
    int sound_output();   // audioq: SOUND OUTPUT (0 Clean, 1 Original), as the page last had it (any thread, b3r_sound_output)
    // nowplaying1: what OTHER MUSIC APPS detects (any thread: native_play's game thread), shown in its description
    void set_now_playing(const std::string& line) {
        std::lock_guard<std::mutex> lock(music_m_);
        now_playing_ = line;
    }

    // Any thread: open (from b3r_settings_open); true while open.
    void request_open() { open_requested_.store(true); }
    bool active() const { return active_.load(); }
    // paintsel (tools/native/paint_select.h): the car paint panel on SELECT CAR -- the CAR tab's paint rows of the page's
    // player (b3settings::paint_panel_content) in a small box over the page (Menu::panel_items), opened and closed by
    // native_play at the VBlank of the CAR PAINT PANEL button (paint_toggle; the presses: NativePad::take_paint_presses,
    // P or a controller's menus Circle while SELECT CAR takes input), closed by Back too and when the page goes. While it
    // is open active() is true as for the page (the game sees a neutral pad); it reads the pad's reports queued on the
    // VBlank grid (NativePad::panel_feed); its edits apply at once (frame() returns true) and the ini is saved when it
    // closes, as the page's.
    bool panel_open() const { return paint_open_.load(); }
    // Any thread (native_play's game thread, b3r_paint_game): the game's own colour changed on SELECT CAR for `player`
    // (0 / 1): that player's CAR PAINT goes back to Game at the next frame (saved), so the car shows the colour chosen
    void paint_game(int player) { if (player == 0 || player == 1) paint_game_pending_.fetch_or(1 << player); }
    // Any thread (native_play's game thread, b3r_paint_toggle, at the VBlank of the panel's button): opens the panel for
    // `player` at once (1: active() from now; the render thread lays it out at its next frame; the pad's reports are
    // queued for it from this VBlank: NativePad::panel_feed), or asks an open one to close (0); -1 while PC SETTINGS is open
    int paint_toggle(int player);

    // Render thread, once per frame: opens on request, runs the model on `pad` (a DS2 report), returns true when the
    // settings changed (the caller applies `settings()`). Saves `ini` when the page closes.
    // mac5: `mouse` (optional) the window's pointer this frame (MenuMouse::page_pointer: fractions of an output of
    // `aspect`, mapped to the page's 640 x 448 picture, the 4:3 box the page is drawn in); a right click is Back.
    bool frame(const std::array<std::uint8_t, 32>& pad, float seconds, const std::string& ini,
               const MenuMouse::PagePointer* mouse = nullptr, float aspect = 4.0f / 3.0f);
    b3settings::Settings& settings() { return settings_; }
    // The settings as read from the ini at the start (settingsui: the page then writes only the keys it changed, merged
    // into the file as it is at the close: the launcher's edits meanwhile stay)
    void load_settings(const b3settings::Settings& s) {
        settings_ = saved_ = s;
        if (!started_set_) started_ = s, started_set_ = true;   // nextstart: the values this run started with
        publish_music();
        loaded_.store(true);
    }
    void publish_music();   // (pcpage.cpp: the custom soundtrack's values for the game's thread)
    // extrastart: the renderer has loaded the settings (before that, settings() holds the defaults)
    bool loaded() const { return loaded_.load(); }
    // nextstart: PC SETTINGS' APPLY AND RESTART chosen (once: taken by native_play through b3r_restart_requested)
    bool take_restart() { return restart_requested_.exchange(false); }
    // nextstart: the question box's context, from the window's frame: a race on (APPLY AND RESTART ends it: said), a
    // replay file playing (no APPLY AND RESTART: its settings are the recording's)
    void set_context(bool in_race, bool replay_playback) { in_race_ = in_race, replay_playback_ = replay_playback; }
    // hdkey (1.2): the language our own text is shown in ("" English): LANGUAGE when the disc has it (render thread; hdkey's notice)
    std::string ui_language();
    // mac5: the tab buttons' labels follow the prompts' device (L1 / R1, LB / RB, L / R, the bound keys)
    void set_prompts(PromptDevice d, const PromptKeys& keys) { prompt_device_ = d; prompt_keys_ = keys; }
    // Offscreen shots: the page's animation clock in game time (b3settings::Menu::set_clock)
    void set_clock(float seconds) { menu_.set_clock(seconds); paint_menu_.set_clock(seconds); }
    // sdk2b, any thread (native_play through b3r_mod_*): the code mods' settings rows (b3mod::rows_to_text; the MODS
    // tab), a value changed by the game side (a script, a replay), and the page's changes for the game, one at a time
    // ("<mod>.<key>=<value>"; 0: none), which native_play applies at its next VBlank
    void set_mod_rows(const std::string& text);
    void set_mod_value(const std::string& qualified, const std::string& value);
    int take_mod_change(char* out, int n);
    // remap1 (src/native/input_bindings.h): the control bindings from controls.ini at `path` (empty or no file: the
    // defaults; an empty path saves nothing), handed to NativePad. CONTROLS > BINDINGS edits them: each change is
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
    void controls_changed(const std::string& what, const std::string& shown = {});   // shown: `what` translated (polish122)
    void controls_family();   // padlayout: each player's pad family (the layout Auto shows) from the window's pads
    // rebind2: the shortcuts the page lists for a device (the keyboard: all; a controller: the ones that take a button)
    static std::vector<b3native::input::PortAction> port_list(b3native::input::Device d);
    // photoblur: a port action's own CONTROLS row id (b3settings kActionCtlPhotoFirst..; 0: positional from kActionCtlFirst)
    static std::uint32_t port_row_id(b3native::input::PortAction a);
    bool reset_all_armed_ = false;   // RESET ALL CONTROLS asks first
    b3native::input::PadStyle pad_style() const;
    void set_local_status(std::string s);

    const HostFont* font_for(std::uint8_t flags, const HostFonts& fonts,
                             const std::map<std::uint64_t, std::shared_ptr<const HudTexture>>& textures) const;

    std::atomic<bool> open_requested_{false}, active_{false};
    // paintsel: the car paint panel (its own menu model: the page's focus memory stays the page's)
    std::atomic<bool> paint_open_{false};
    std::atomic<int> paint_game_pending_{0};   // a bit per player: the game's colour changed (b3r_paint_game)
    std::atomic<bool> paint_close_asked_{false};
    std::atomic<int> paint_player_{0};
    bool paint_laid_ = false;   // the render thread has opened the panel's menu (paint_menu_.open)
    b3settings::Menu paint_menu_;
    std::uint64_t paint_edits_ = 0;
    bool paint_step(const std::string& ini);   // the game's colour, the panel laid out, the page's going: true when settings changed
    bool paint_frame(const std::array<std::uint8_t, 32>& pad, float seconds, const std::string& ini, const MenuMouse::PagePointer* mouse);
    bool paint_step_report(const std::array<std::uint8_t, 32>& pad, float seconds, const MenuMouse::PagePointer* mouse);
    std::array<std::uint8_t, 32> paint_last_ = b3native::input::neutral_sample();   // the last report taken (held keys repeat)
    bool paint_back_ = false;   // Back taken from a report: the panel closes after this frame's reports
    int paint_layer_ = 0;       // paint2: the layer the panel's colour rows edit (b3settings kLayerBody ..)
    b3settings::ColourPicker paint_picker_;   // paint2: the colour picker (on: it takes the pad)
    int paint_saved_custom_ = 0;               // the layer's switch when the picker opened (BACK puts it back)
    float paint_aspect_ = 4.0f / 3.0f;
    void paint_close(const std::string& ini, const char* why);
    bool save_now(const std::string& ini, std::string& why);   // the merged save (as the page's close); true when saved
    b3settings::Settings settings_;
    b3settings::Settings saved_;   // settingsui: what the ini held when last read or written by the page
    // nextstart: the question box (page.h restart_ask_content) when the page closes with a next-start row changed
    b3settings::Settings started_, visit_;   // the values at this run's start and at the page's opening
    bool started_set_ = false;
    std::atomic<bool> loaded_{false};   // extrastart: load_settings has run
    int visit_next_profile_ = 0;             // SAVE FILE AT START (active-slot.txt) at the page's opening
    bool visit_capture_ = false;             // (taken with the first rows after the opening)
    bool ask_open_ = false;
    b3settings::Menu ask_menu_;
    std::vector<b3settings::PendingChange> ask_changes_;   // translated
    std::atomic<bool> restart_requested_{false};
    bool in_race_ = false, replay_playback_ = false;
    bool ask_frame(const b3settings::Input& in, const b3settings::Env& env, const std::string& ini);
    bool close_page(const std::string& ini);   // the page's close: the controls flushed, the ini merged and saved
    b3settings::Menu menu_;
    std::uint64_t edits_ = 0, serial_ = 0, built_serial_ = ~0ull;
    std::shared_ptr<const HostUi> built_;
    std::uint32_t built_fonts_ = 0;
    bool warned_ = false;
    PromptDevice prompt_device_ = PromptDevice::kPlayStation;
    PromptKeys prompt_keys_{};
    // settingsui: the prompt bar's and the tab bar's button glyphs (prompts.h paint_prompt_glyph, and the D-pad / arrow
    // keys drawn here), one picture for the device in use; a cell per name (b3settings kGlyphPrefix names)
    struct GlyphCell { float u0 = 0, v0 = 0, u1 = 0, v1 = 0, aspect = 1; };
    std::map<std::string, GlyphCell> glyph_cells_;
    std::shared_ptr<const HostUi::Image> glyph_image_;
    PromptDevice glyph_device_ = PromptDevice::kPlayStation;
    PromptKeys glyph_keys_{};
    std::uint32_t glyph_serial_ = 0;
    PromptKeys page_prompt_keys() const;   // the keyboard: the menus' keys, the section keys on L2 / R2
    void build_glyphs();
    SaveHooks save_;
    std::mutex save_m_;
    std::string save_local_;     // a dialog's own result (no file chosen, no dialog), until the next request's result
    int save_serial_seen_ = 0;
    int alert_serial_seen_ = 0;   // fsrobust: save_alert's
    int save_profile_ = 0;
    std::string save_folder_;
    bool hd_pending_ = false;    // mac5: a folder chosen (hd_pending_dir_), to apply on the page's thread
    std::string hd_pending_dir_, hd_status_;
    bool music_pending_ = false;  // the music folder chosen (music_pending_dir_), to apply on the page's thread
    std::string music_pending_dir_, music_status_;
    std::mutex music_m_;         // the published music settings (music_folder_pub_, playlist_mode_pub_)
    // remap1 (render thread)
    std::string controls_path_;
    bool controls_unsynced_ = false;   // fsrobust: controls.ini written since the page opened, not yet flushed to the disk
    // 1.0.1 LANGUAGE: the imported languages (<preferences>/languages, beside controls.ini), scanned once
    std::vector<std::pair<std::string, std::string>> languages_;
    bool languages_scanned_ = false;
    void scan_languages();   // languages_, once
    // portable mode (1.3.0): the PORTABLE MODE row's state (found once: a write probe of the install folder), shown
    // only when the page edits the player's own data folder (controls.ini there: a window run of the native game)
    bool portable_known_ = false;
    b3settings::Env::Portable portable_;
    std::string portable_dir_;   // the data folder the request goes into (UTF-8)
    void portable_scan();
    b3native::input::Controls controls_ = b3native::input::default_controls();
    std::size_t ctl_player_ = 0, ctl_device_ = 0, ctl_view_ = 0;
    b3native::input::RebindFlow flow_;
    bool flow_left_ = true, flow_right_ = true, flow_confirm_ = true, flow_back_ = true;   // the conflict's edges
    std::string ctl_status_;
    std::string music_folder_pub_;
    std::string now_playing_;   // nowplaying1 (music_m_)
    int playlist_mode_pub_ = 0;
    int music_volume_pub_ = 100;   // musicvol1: CUSTOM MUSIC VOLUME
    int sound_output_pub_ = 0;     // audioq: SOUND OUTPUT
    int music_loudness_pub_ = 1;   // musicloud: CUSTOM MUSIC LOUDNESS
    std::mutex mod_m_;            // sdk2b: the code mods' rows and the changes made here
    std::vector<b3mod::SettingRow> mod_rows_;
    std::deque<std::string> mod_changes_;
};

}  // namespace b3replay

#endif
