// The PC SETTINGS page (settings1): its rows built from Settings, edits applied back, and the menu model that turns
// pad / keyboard / mouse input into edits and lays the page out as UI items in the game's 640 x 448 menu picture.
//
// The menu model (class Menu, src/b3settings/menu.cpp) is written from its behaviour spec alone
// (docs/specs/settings-page.md): edges and the hold repeat on the caller's clock, focus kept by
// row id, per-tab focus memory, a scrolled row window, the pointer and drag rules, and a look of its own after the
// game's option pages (slanted blue panels, the italic menu face, the L1 / R1 tab prompts).
#ifndef B3SETTINGS_PAGE_H
#define B3SETTINGS_PAGE_H

#include "b3settings/settings.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace b3settings {

enum class RowKind : std::uint8_t { kSection, kToggle, kChoice, kSlider, kAction };

struct Row {
    std::uint32_t id = 0;
    RowKind kind = RowKind::kChoice;
    std::string label;
    std::vector<std::string> values;   // Toggle: Off / On; Choice: every value; Action: one text
    std::size_t value = 0;
    std::size_t levels = 0;            // Slider
    std::string level_text;
    std::string description;
    bool disabled = false;
    // padpaint: a colour swatch drawn in the row, left of its control (0xRRGGBB); none when negative
    std::int32_t swatch = -1;
    // paintsel: a colour per value (0xRRGGBB; the paint panel draws the value bar as a row of these colours, the value's
    // own larger); empty: the bar as usual. Only the paint panel's rows have them (paint_panel_content).
    std::vector<std::int32_t> chips;
};

struct Tab {
    std::string label;
    std::vector<Row> rows;
};

struct Content {
    std::vector<Tab> tabs;
    std::string popup;
    // 1.1: the page's own words around the rows (Menu::items draws them), translated with the rows (translate below)
    std::string title = "SETTINGS";
    bool title_menu_face = false;   // nextstart: the title in the menu face (the heading face lacks letters: X, accents)
    std::string footer_keys = "UP / DOWN  SELECT     LEFT / RIGHT  CHANGE     CONFIRM  CHOOSE";
    std::string footer_back = "BACK  CLOSE AND SAVE";
    // paint2: the paint panel's colour picker (Menu::panel_items draws it in place of the description while `on`): the
    // colour as hue (0..360), saturation and brightness (0..1), and FINE (the slow steps)
    struct Picker { bool on = false; float h = 0, s = 0, v = 0; bool fine = false; } picker;
    // settingsui: the prompt bar under the description (Menu::items): each prompt a glyph (or two) and its words, the
    // glyphs drawn by the host (Item kImage, kGlyphPrefix + the button's name, measured with kGlyph). `prompt_device`:
    // what the last input came from (the bar shows its buttons; the mouse: its own words, no glyphs).
    enum class PromptDevice : std::uint8_t { kPad, kKeyboard, kMouse };
    PromptDevice prompt_device = PromptDevice::kPad;
    struct Words {   // translated with the rows
        std::string move = "MOVE", change = "CHANGE", choose = "CHOOSE", close = "CLOSE AND SAVE", section = "SECTION",
                    rebind = "REBIND", add = "ADD", remove = "REMOVE", reset = "DEFAULT", click = "CLICK", wheel = "WHEEL",
                    scroll = "SCROLL", right_click = "RIGHT CLICK", close_short = "CLOSE";
        // paint2: the colour picker's prompts
        std::string hue = "HUE", shade = "SHADE", fine = "FINE", accept = "OK", cancel = "CANCEL";
    } words;
};

// What the host can offer (rows for things it lacks are left out or disabled).
struct Env {
    bool hd_pack = false;              // an HD pack was given
    bool hd_pack_loaded = false;       // mac5: one is in use this run (its replacements indexed)
    std::string hd_pack_status;        // mac5: the last folder choice's result
    bool xbox_textures = false;        // xboximport: an Import Xbox textures pack is there (the XBOX TEXTURES row)
    bool music = false;                // the host plays a music folder (INTERFACE: the MUSIC section; native_play)
    std::string music_status;          // the music folder: the last choice's result
    std::string now_playing;           // nowplaying1: what OTHER MUSIC APPS detects now (native_play), empty: not reading
    // 1.0.1 LANGUAGE (b3asset/language.h): the imported languages (code, name), as <preferences>/languages holds them;
    // the row offers English and these. `language_import`: the host can import from a disc (IMPORT LANGUAGES row)
    std::vector<std::pair<std::string, std::string>> languages;
    bool language_import = false;
    std::string language_status;       // the last import's result
    // 1.3.0 DJ VOICE (b3asset/language.h dj_*): the launcher's import rows, one per language with a DJ of its own that the
    // last chosen disc has or that is imported (kActionDjFirst + its index in dj_codes(): FR GE IT SP). Host-only rows
    // (language_import): the in-game page has none.
    struct DjVoice {
        std::string code, name;            // "GE", "Deutsch (German)"
        std::uint64_t disc_bytes = 0;      // on the chosen disc (0: not there)
        std::uint64_t imported_bytes = 0;  // imported (0: not)
        int progress = -1;                 // 0..100 while it is being imported, else -1
        bool training = false;             // the race training videos' pictures (kActionTrainingFirst + index), not the DJ
    };
    std::vector<DjVoice> dj_voices;
    std::string dj_status;             // the last import's or removal's result
    // save1: the native game's card is a profile file (SAVES tab: export / import); profile 0 = no card this run
    int profile = 0;
    int next_profile = 0;              // active-slot.txt: the file the next start uses (0: as `profile`)
    std::array<bool, 5> profile_used{};   // profile-N.sav (or an older profile-N folder) exists, N = index + 1
    std::string saves_folder;
    std::string save_status;           // the last export / import's result
    // sdk2b: the code mods' settings rows (the MODS tab, after SAVES; none: no tab). Their ids are kModRowBase + n and
    // kSectionModBase + n (a section per mod); apply_edit leaves them to the host (src/b3replay/pcpage.cpp).
    std::vector<Row> mod_rows;
    // tune4: tabs the host adds after the page's own (the DEVELOPER tab while DEVELOPER MODS is on and the console is
    // in this run: src/b3replay/tunepage.h); their rows' edits are the host's (apply_edit leaves them)
    std::vector<Tab> extra_tabs;
    // settingsui: the launcher's Settings page (its own pages hold the controls, the music folder and the HD pack: a row
    // that opens that page stands in for their rows, kActionOpen* below)
    bool launcher = false;
    bool graphics_api = false;         // d3d12: the GRAPHICS API row (Windows: Vulkan / Direct3D 12)
    // remap1 (src/native/input_bindings.h): the CONTROLS section, filled by a host that remaps (native_play's page).
    // The host makes the texts; the rows' edits (ids below) are the host's to act on (apply_edit leaves them).
    struct Controls {
        bool on = false;
        std::size_t player = 0;        // 0 / 1; rebind2: 2 the port actions (window shortcuts, photo mode: PortAction)
        std::size_t device = 0;        // 0 keyboard, 1 controller
        std::size_t view = 0;          // 0 driving, 1 menus (the keyboard's; padlayout: the controller's too)
        // padlayout (1.2.1): CONTROLLER LAYOUT (the controller device): 0 Auto, 1 Xbox, 2 PlayStation, 3 Nintendo, 4 Custom, and
        // Auto's value as the host words it for the player's pad (input_bindings.h auto_text: "Auto: Xbox controller")
        std::size_t layout = 0;
        std::string layout_auto = "Auto: no controller, Xbox layout";
        // `id`: the row's id (0: kActionCtlFirst + its place among the rows without one; photoblur's photo actions
        // take kActionCtlPhotoFirst + n)
        struct Action { std::string label, value, description; std::uint32_t id = 0; };
        std::vector<Action> actions;   // in input_bindings.h action_order()
        std::string status;            // the last change
        // pad1: the player's gyro steering (input_bindings.h GyroControls; rows only with the controller device):
        // each a value index; the recentre button's labels in gyro_recentre_choices()'s order, in the pad's style
        bool gyro = false;
        std::size_t gyro_mode = 0, gyro_axis = 0, gyro_sensitivity = 3, gyro_dead_zone = 2, gyro_recentre = 1;
        std::vector<std::string> gyro_recentre_labels;
    } controls;
    // portable mode (1.3.0, include/b3port/user_dir.h): PORTABLE MODE (GAME > STARTING AND LOADING; the launcher's
    // LAUNCHER tab: portable_row). Shown only when the host edits the player's own data folder on a system that allows
    // it (not macOS, an AppImage or a Flatpak). Its edit is the host's (apply_edit leaves it): the game asks the launcher
    // to move the data at its next start, the launcher moves it.
    struct Portable {
        bool shown = false;
        bool on = false;            // the install is portable now (<install>/appdata is there)
        int request = -1;           // the game: what the launcher is asked to do at its next start (-1 nothing, 0 off, 1 on)
        std::string disabled;       // shown disabled, for this reason (a sentence of its own: translated)
    } portable;
};
enum : std::uint32_t { kModRowBase = 0x00200000u, kModRowEnd = 0x00300000u, kSectionModBase = 0xFFFE0000u };

// save1: the SAVES tab's rows the host acts on (apply_edit leaves them): the actions (Edit::action set), and the save
// file choice (Edit::value = the file's index, 0 for profile-1.sav)
enum : std::uint32_t { kActionExportSave = 100, kActionImportSave = 101, kActionOpenSaveFolder = 102, kRowSaveFile = 103,
                           kRowSaveScreens = 230 /* autosave1: SAVE SCREENS (SAVES tab; a live setting: apply_edit takes it) */ };
// mac5: the HD pack folder (QUALITY): choose one (the system's folder dialog), or none
enum : std::uint32_t { kActionChooseHdPack = 104, kActionClearHdPack = 105 };
// the custom soundtrack's folder (MUSIC): choose one (the system's folder dialog), or none
enum : std::uint32_t { kActionChooseMusic = 106, kActionClearMusic = 107 };
// settingsui: the launcher's rows that open one of its pages (Env::launcher): CONTROLS > BINDINGS (Controllers),
// AUDIO > MUSIC (Music), GRAPHICS > TEXTURES (HD Textures). Not settings rows: apply_edit leaves them to the launcher.
enum : std::uint32_t { kActionOpenControllers = 380, kActionOpenMusic = 381, kActionOpenHdPage = 382 };
// settingsui: the ids of two rows a host looks for in the (translated) content: HD TEXTURES and XBOX TEXTURES
enum : std::uint32_t { kRowIdHdTextures = 22, kRowIdXboxTextures = 383 };
// tune4: the DEVELOPER tab's rows (built by the host: Env::extra_tabs). Its actions, and the variables' rows
// (kRowTuneFirst + 64 x the variable's index + its element; Edit::direction: -1 / +1 a step, 0 its default)
enum : std::uint32_t { kActionTuneSavePreset = 108, kActionTuneOverlay = 109, kRowTuneFirst = 0x01000000u };
// remap1: the CONTROLS section (Env::controls): the player, device and keyboard view choices (Edit::value), an action
// row per binding (kActionCtlFirst + its index: confirm rebinds, right adds an input, left removes the last), reset
enum : std::uint32_t { kRowCtlPlayer = 300, kRowCtlDevice = 301, kRowCtlView = 302, kRowCtlStatus = 303, kActionCtlReset = 304,
                       kActionCtlFirst = 310, kActionCtlLast = 349, kActionCtlResetAll = 350,
                       kRowCtlLayout = 351 /* padlayout: CONTROLLER LAYOUT (Edit::value: 0 Auto .. 3 Custom) */ };
// photoblur (1.3.0): a second block of CONTROLS action rows, 410-419, for photo mode's shortcuts added after the
// 310-349 block (its rows are positional and nearly full): 410-412 the motion blur's MOTION, FASTER / SLOWER SHUTTER;
// 413 todrace's TIME OF DAY PER RACE; 414 paintsel's CAR PAINT PANEL; 415 / 416 gearbox's SHIFT UP / SHIFT DOWN;
// 417-419 party1's MOON JUMP / CRASH / AI DRIVER (kActionCtlPartyFirst). The host gives these rows their ids (Env::Controls::Action::id).
enum : std::uint32_t { kActionCtlPhotoFirst = 410, kActionCtlRaceTime = 413, kActionCtlPaint = 414, kActionCtlShiftUp = 415,
                       kActionCtlShiftDown = 416, kActionCtlPhotoLast = 419,
                       kActionCtlReplay = 622 };   // replayview (620-629 its rows)
// party1: PARTY MODES' buttons, MOON JUMP 417, CRASH 418, AI DRIVER 419
enum : std::uint32_t { kActionCtlPartyFirst = 417 };
// replaycams: the replay viewer's REPLAY CAMERA - / +, REPLAY CAR - / +, FREE CAMERA (660-669 are replaycams'; 660 is
// REPLAY CAMERA CUTS)
enum : std::uint32_t { kActionCtlReplayFirst = 661, kActionCtlReplayLast = 665 };
inline bool is_ctl_action(std::uint32_t id) {
    return (id >= kActionCtlFirst && id <= kActionCtlLast) || (id >= kActionCtlPhotoFirst && id <= kActionCtlPhotoLast) ||
           (id >= kActionCtlReplayFirst && id <= kActionCtlReplayLast);
}
// rebind2: Edit::direction 2 on an action row is "put this one back to its default" (Input::reset: SQUARE)
// pad1: the CONTROLS section's gyro steering rows (the controller device; Edit::value = the value index: mode 0 off /
// 1 mix / 2 replace, axis 0 roll / 1 yaw, sensitivity (value + 1) x 25 %, dead zone value degrees, recentre the index in
// input_bindings.h gyro_recentre_choices()); the host applies them to the player's controls
enum : std::uint32_t { kRowCtlGyroMode = 305, kRowCtlGyroAxis = 306, kRowCtlGyroSensitivity = 307, kRowCtlGyroDeadZone = 308,
                       kRowCtlGyroRecentre = 309 };

// portable mode (1.3.0): PORTABLE MODE (Env::Portable; Edit::value 0 off, 1 on: the host's). 405-409 are portable mode's.
enum : std::uint32_t { kRowPortable = 405 };
// The row as the game (launcher false: applies at the launcher's next start) or the launcher (true: the move now) shows it
Row portable_row(const Env::Portable& p, bool launcher);

Content build_content(const Settings& s, const Env& env);
// paintsel (tools/native/paint_select.h): the car paint panel on SELECT CAR -- one tab with the CAR tab's paint rows of
// `player` (0: CAR PAINT, COLOUR, PAINT RED / GREEN / BLUE, FINISH, DECALS; 1: player 2's, labelled as player 1's, under
// the title PLAYER 2 CAR PAINT; FINISH is both players'). The rows' ids, values and edits are the CAR tab's (apply_edit
// takes them); the COLOUR row carries its ready colours as chips. Untranslated (translate() as the page's).
Content paint_panel_content(const Settings& s, int player);   // (paintsel's: the body layer, no picker)
// paint2 (2026-11-22): the panel's layers -- BODY (CAR PAINT and its RGB), DECALS, WHEELS, BOOST GLOW (settings.h
// decal_* / wheel_* / flame_*), per player, one set for every car -- and its rows: LAYER (which layer the colour rows
// edit), COLOUR (Game, the ready colours, Custom), COLOUR PICKER, PAINT RED / GREEN / BLUE, FINISH, DECALS, FACTORY
// COLOURS. `picker`: the picker's state to draw (Content::picker).
enum : int { kLayerBody = 0, kLayerDecals = 1, kLayerWheels = 2, kLayerFlame = 3, kPaintLayers = 4 };
Content paint_panel_content(const Settings& s, int player, int layer, const Content::Picker& picker = {});
// paint2 row ids (560-599 are paint2's): a layer's COLOUR row kRowLayerColour + 4 x player + layer (560-567; PC SETTINGS
// shows the decals', wheels' and flame's: 561-563, 565-567); its RGB rows kRowLayerRgb + 3 x (4 x player + layer) +
// channel (570-593); the panel's LAYER row (its edit is the host's), the picker's action (the host's) and FACTORY
// COLOURS (kActionPaintFactory + player, 596-597: apply_edit runs paint_factory)
enum : std::uint32_t { kRowLayerColour = 560, kRowLayerRgb = 570, kRowPaintLayer = 594, kActionPaintPicker = 595, kActionPaintFactory = 596 };
// A player's layer as settings fields: the custom switch (0 the game's, 1 custom) and the colour (0..255 each)
struct PaintLayerRef { int* custom = nullptr; int* rgb[3] = {}; };
PaintLayerRef paint_layer(Settings& s, int player, int layer);
// FACTORY COLOURS: every layer of `player` back to the game's and DECALS on (FINISH, both players', stays)
void paint_factory(Settings& s, int player);
// HSV <-> RGB (h 0..360, s / v 0..1; rgb 0..255)
void rgb_to_hsv(const int rgb[3], float& h, float& s, float& v);
void hsv_to_rgb(float h, float s, float v, int rgb[3]);
// paint2: the colour picker's model (the paint panel; pure: tests). D-pad / left stick left-right the saturation, up-down
// the brightness, L1 / R1 the hue (held: they keep going), SQUARE FINE (a quarter of the speed), CROSS keeps the colour,
// BACK puts the one before back; a pointer on the square or the bar (Menu::kPicker*) sets them where it is held.
struct PickerInput {
    bool left = false, right = false, up = false, down = false, hue_down = false, hue_up = false;
    bool fine = false, accept = false, cancel = false;   // held states (the model takes their presses)
    std::optional<std::array<float, 2>> pointer;         // picture pixels
    bool pointer_held = false;
    float sq[4] = {}, bar[4] = {};                       // where the square and the bar are (Menu::picker_boxes)
    float seconds = 1.0f / 60.0f;
};
class ColourPicker {
public:
    void open(const int rgb[3]);                 // from the layer's colour (its held buttons act only after release)
    bool on() const { return on_; }
    enum class Result : std::uint8_t { kNone, kChanged, kAccepted, kCancelled };
    Result step(const PickerInput& in);          // kChanged: rgb() moved; kCancelled: put saved() back
    const int* rgb() const { return rgb_; }
    const int* saved() const { return saved_; }
    Content::Picker view() const { return Content::Picker{on_, h_, s_, v_, fine_}; }
private:
    bool on_ = false, fine_ = false, was_fine_ = true, was_accept_ = true, was_cancel_ = true;
    float h_ = 0, s_ = 0, v_ = 0;
    int rgb_[3] = {}, saved_[3] = {};
};
// paintsel: the ready colours of the COLOUR rows (0xRRGGBB, in the row's order)
std::vector<std::int32_t> paint_preset_colours();
// settingsui: OPEN THE LAUNCHER AT START (GAME > STARTING AND LOADING in the game; the launcher's own LAUNCHER tab, so
// build_content leaves it out with Env::launcher). An edit goes through apply_edit as any row's.
Row launcher_at_start_row(const Settings& s);

// settingsui: every row, action and section id of `c` (rows' ids and kSection-based section ids alike) is used once;
// the ids that are not (each once). For the tests: the 280 / 290 / 350 collisions.
std::vector<std::uint32_t> duplicate_ids(const Content& c);

// 1.0.1: LANGUAGE (INTERFACE; its edit is the host's: language_edit maps the shown value to a code) and IMPORT
// LANGUAGES (an action: the host's disc dialog)
enum : std::uint32_t { kRowLanguage = 270, kActionImportLanguages = 271 };
// 1.3.0: VOICES (INTERFACE, after LANGUAGE: a setting, apply_edit takes it) and the launcher's import rows (actions:
// kActionDjFirst / kActionTrainingFirst + the language's index in b3asset::language::dj_codes(); the host imports,
// cancels or removes)
// d3d12: GRAPHICS API (GRAPHICS > IMAGE, last; Windows only: Env::graphics_api; 670-679 are d3d12's)
enum : std::uint32_t { kRowGraphicsApi = 670 };
enum : std::uint32_t { kRowDjVoice = 384, kActionDjFirst = 385, kActionDjLast = 388, kActionTrainingFirst = 420, kActionTrainingLast = 423 };

// 1.1: the port's own text in the player's language (include/b3settings/i18n.h). `language` is Settings::language (a
// disc code: FR GE IT SP DU SW; empty or any other: English, unchanged). Every label, value, level text and description
// sentence of `c` that the language's table has is replaced; what it lacks (paths, statuses, a mod's rows) stays as it is.
void translate(Content& c, const std::string& language);
// The language the page is shown in: Settings::language when it is one of env.languages (imported), else English ("").
std::string ui_language(const Settings& s, const Env& env);
// The texts of `c` (labels, values, sentences) that `language`'s table lacks; none when it has no table. For the
// tests: every word the page shows has its translation.
std::vector<std::string> untranslated(Content c, const std::string& language);
// One text in `language` (a label, a value, or a description: sentence by sentence); itself when untranslated.
std::string translate_text(const std::string& text, const std::string& language);
// A `{}` entry's text: `pattern` (the table's key as it is, "(NOT SAVED: {})") translated, then `arg` put in its `{}`.
// The argument never takes part in the lookup (a reason or a number that happens to read as other text).
std::string translate_format(const std::string& pattern, const std::string& arg, const std::string& language);

// nextstart: the rows read only at the game's start (their help: "Applies at the next start"). When PC SETTINGS closes
// with one of them changed since it opened, a question box (a panel: Menu::panel_items) asks KEEP (the default: applies
// at the next start), APPLY AND RESTART (saved, then the game started again by native_play) or REVERT (the old values
// back); a row whose value differs from the game's start shows it is pending (mark_pending). 640-649 are nextstart's.
enum : std::uint32_t { kActionAskKeep = 640, kActionAskRestart = 641, kActionAskRevert = 642, kRowAskChange = 643 };
bool next_start_row(std::uint32_t id);
struct PendingChange {
    std::uint32_t id = 0;
    std::string label, before, after;   // the row's label and its value then and now, as the page shows them
};
// The next-start rows whose shown value differs between (`from`, `env_from`) and (`to`, `env_to`), in the page's order
// (SAVE FILE AT START: Env::next_profile, a file; the rest: Settings)
std::vector<PendingChange> next_start_changes(const Settings& from, const Env& env_from, const Settings& to, const Env& env_to);
// The next-start rows' settings as `from` has them (SAVE FILE AT START is the caller's: a file)
void revert_next_start(Settings& s, const Settings& from);
// The question box's content: the changes (translated by the caller with the rest), `in_race`: a line that a race in
// progress ends with a restart; `can_restart` false (a replay file's playback): no APPLY AND RESTART
Content restart_ask_content(const std::vector<PendingChange>& changes, bool in_race, bool can_restart);
// The page's next-start rows whose value differs from the game's start (`started`, `env_started`): their value marked
// " *" and a sentence before the help, in `language` (after translate: the value's own text is translated first)
void mark_pending(Content& c, const Settings& started, const Env& env_started, const std::string& language);

struct Edit {
    std::uint32_t id = 0;
    std::size_t value = 0;
    int direction = 0;
    bool action = false;
};
// Applies one edit of build_content's rows; false when the id is not a settings row.
bool apply_edit(Settings& s, const Edit& e);
// 1.0.1: a LANGUAGE edit into `s` (value 0 English, n the env's n-th imported language); false for any other edit.
bool language_edit(Settings& s, const Env& env, const Edit& e);

// One poll of input (held states; the model makes edges and the hold repeat). Pointer in picture pixels.
struct Input {
    bool up = false, down = false, left = false, right = false;
    bool confirm = false, back = false, previous_tab = false, next_tab = false;
    // devnav1: the previous / next section (PageUp / PageDown; a long tab such as DEVELOPER), held repeats as up / down
    bool previous_section = false, next_section = false;
    // rebind2: put the focused control row back to its default (the CONTROLS rows; SQUARE, a keyboard's key for it too)
    bool reset = false;
    std::optional<std::array<float, 2>> pointer;
    bool pointer_moved = false, click = false, click_held = false;
    int wheel = 0;
    float seconds = 1.0f / 60.0f;      // since the previous poll: the caller's (deterministic) clock
};

struct Frame {
    std::vector<Edit> edits;
    bool close = false;
};

// UI items in the 640 x 448 picture (settings-page.md §1). The vocabulary is the page's own; src/b3replay/pcpage.cpp
// draws all of it.
enum class ItemKind : std::uint8_t { kFontDirectory = 0, kRect = 1, kText = 2, kImage = 3 };
// Text flags: the face (a size field: regular, small, large, or the slanted and widened menu face), the alignment in the
// box (left by default), the heading face, no drop shadow. Texts are clipped to their box.
constexpr std::uint8_t kTextRegular = 0u, kTextSmall = 1u, kTextLarge = 2u, kItalic = 3u, kTextSizeMask = 3u,
                       kAlignRight = 1u << 2, kAlignCentre = 1u << 3, kHeadingFont = 1u << 4, kNoOutline = 1u << 5;
// settingsui: Measure with kGlyph measures a button glyph (the text is the button's name, as after kGlyphPrefix) at
// that height; the prompt bar's glyphs are Item kImage named kGlyphPrefix + the name: "cross", "circle", "square",
// "triangle", "l1", "r1", "l2", "r2", "l3", "r3", "start", "select", "dpad-ud", "dpad-lr" (a pad's D-pad, the keyboard's
// arrows), "section-up", "section-down" (the section keys: the keyboard's, or the right stick)
constexpr std::uint8_t kGlyph = 1u << 6;
constexpr const char* kGlyphPrefix = "glyph:";
// Rect flags: a parallelogram leaning as the menu face does (each point moved right by kItalicSlant x its height above
// the bottom edge), and a shaded fill (the colour at the top, darker towards the bottom).
constexpr std::uint8_t kRectSlant = 1u << 0, kRectShade = 1u << 1;
// The menu face: slanted by kItalicSlant (x per unit of height above the box's bottom edge), widened by kItalicWidth.
constexpr float kItalicSlant = 0.2f, kItalicWidth = 1.08f;
// Image names (relative to the art directory): the value and scroll arrows (drawn as triangles) and the tab prompts
// (drawn as the prompt device's L1 / R1 labels).
constexpr const char* kImageArrowLeft = "arrow-left.png";
constexpr const char* kImageArrowRight = "arrow-right.png";
constexpr const char* kImageArrowUp = "arrow-up.png";
constexpr const char* kImageArrowDown = "arrow-down.png";
constexpr const char* kImagePromptL1 = "prompt-l1.png";
constexpr const char* kImagePromptR1 = "prompt-r1.png";
struct Item {
    ItemKind kind = ItemKind::kRect;
    std::uint8_t flags = 0;
    float x = 0, y = 0, w = 0, h = 0;
    std::uint32_t colour = 0xFFFFFFFFu;   // RGBA, R in the low byte
    std::string text;                     // text, or an image's file name (relative to the art directory)
};

// The width a text of letter height `height` takes when drawn (menu text widened, slant not included).
using Measure = std::function<float(std::string_view text, float height, std::uint8_t flags)>;
float fallback_measure(std::string_view text, float height, std::uint8_t flags);

class Menu {
public:
    void open(std::size_t tab = 0);
    std::size_t tab() const { return tab_; }
    std::size_t focus() const { return focus_; }
    Frame update(const Content& content, const Input& in);
    // The running animation clock (the focused row's pulse), for a caller that draws a page at a known game time:
    // an offscreen shot sets it from the shot's poll so that the picture does not depend on host timing.
    void set_clock(float seconds);
    // settingsui: every button and direction held now acts only after it is released (as the press that opened the
    // page): the host calls it when a rebind took an input, so that the input taken does not also act on the page
    void hold_until_released();
    // nextstart: the pointer on the rows as panel_items lays them out (a menu shown as a panel: the question box)
    void set_panel_pointer(bool on) { panel_pointer_ = on; }
    // The page's items; images are named relative to `art`, fonts are in `fonts` (a FontDirectory item first).
    std::vector<Item> items(const std::string& fonts, const std::string& art, const Measure& measure) const;
    // paintsel: the content as a small panel over a game page (the car paint panel on SELECT CAR): no backdrop and no
    // tabs, the first tab's rows in a box at the picture's left (kPanelBox), its title, the focused row's description and
    // the prompt bar below. The rows must fit (no scrolling: at most kVisible); the pointer is not used (Input::pointer
    // left empty). The page's look: the same faces, colours and slanted bars, smaller.
    std::vector<Item> panel_items(const std::string& fonts, const std::string& art, const Measure& measure) const;
    static constexpr float kPanelX0 = 20.0f, kPanelX1 = 292.0f, kPanelY0 = 84.0f;   // the panel's box (640 x 448 picture)
    static constexpr float kPanelRowH = 20.0f;
    // paint2: the colour picker's square (saturation left to right, brightness bottom to top) and hue bar, laid out under
    // the rows at `y` (the description's place): Menu::picker_boxes
    struct PickerBoxes { float sq[4], bar[4]; };   // x, y, w, h
    static PickerBoxes picker_boxes(float y);
    // the y the description (or the picker) starts at for `rows` rows
    static float panel_detail_y(std::size_t rows) { return kPanelY0 + 32.0f + kPanelRowH * float(rows) + 8.0f; }

private:
    // One held direction: acts on the press, then after the repeat delay at the repeat interval. Blocked (no action)
    // until released when it was held as the page opened or while a popup showed.
    // devnav1: with `fast` (up / down, the sections), held past 1.5 s it steps twice as often, past 3 s ~60 a second.
    struct Repeat {
        bool held = false, blocked = true;
        bool fresh = false;   // settingsui: the last step that acted was the press itself (not a repeat)
        float time = 0, next = 0;
        bool step(bool now, float seconds, bool block, bool fast = false);
    };
    const std::vector<Row>* tab_rows() const;
    std::size_t first_selectable() const;
    void focus_row(std::size_t index, bool header);
    void section_step(int direction);   // devnav1: the first row of the next / this or the previous section
    void show_focus(bool header);
    void switch_tab(std::size_t to);
    void scroll(int rows);
    void change(int direction, Frame& out);
    void activate(Frame& out);
    void set_value(std::size_t value, Frame& out);
    void handle_pointer(const Input& in, Frame& out);

    Content content_;
    std::size_t tab_ = 0, focus_ = 0, scroll_ = 0;
    std::uint32_t focus_id_ = 0;
    bool focus_known_ = false;            // focus_id_ names a row (else: the first selectable row)
    std::vector<std::uint32_t> memory_;   // per tab: the focused row's id while the page is open
    std::vector<bool> remembered_;
    std::vector<std::string> tab_labels_;   // settingsui: the tabs the memory is for (another list: the memory starts again)
    bool was_confirm_ = true, was_back_ = true, was_previous_ = true, was_next_ = true, was_reset_ = true;
    Repeat up_, down_, left_, right_, section_up_, section_down_;
    bool dragging_ = false;
    bool panel_pointer_ = false;   // nextstart (set_panel_pointer)
    std::uint32_t drag_id_ = 0;
    float open_time_ = 0, tab_time_ = 0, clock_ = 0;   // since open / the last tab switch / running (animation)
    int tab_from_ = 0;                    // the last tab switch's direction (-1 left, +1 right)
    // settingsui: the tabs' edges as last drawn (each as wide as its label needs, the bar shared out by them; the
    // pointer hits them there). Empty, or another count: equal widths. Written by items() and read by update(): the
    // host calls both on one thread (b3replay live.cpp's loop: PcPage::frame, then PcPage::ui).
    mutable std::vector<float> tab_edges_;
};

}  // namespace b3settings

#endif
