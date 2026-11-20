// The SDL3 event adapter (src/native/input_keys_sdl.h) on synthetic events: SDL's headers only, no SDL library, no
// window. What the native game's window (src/b3replay/nativepad.cpp) passes on to the pad, and what it leaves to the
// viewer (Ctrl / Cmd shortcuts, Alt+Enter).
#include "input_keys_sdl.h"
#include "test.h"

using namespace b3native::input;

namespace {
SDL_Event key(SDL_Scancode sc, bool down, SDL_Keymod mod = SDL_KMOD_NONE, bool repeat = false) {
    SDL_Event e{};
    e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    e.key.scancode = sc;
    e.key.down = down;
    e.key.mod = mod;
    e.key.repeat = repeat;
    return e;
}
bool cross(const KeyPad& k) { return !(k.sample()[1] & (1u << 6)); }   // SCE_PAD2_CROSS = 14: byte 1 bit 6
}  // namespace

TEST(keys_reach_the_pad) {
    KeyPad k;
    CHECK(handle_sdl_event(k, key(SDL_SCANCODE_Z, true)));
    CHECK(cross(k));
    CHECK(handle_sdl_event(k, key(SDL_SCANCODE_Z, true, SDL_KMOD_NONE, true)));   // a repeat: used, no change
    CHECK(cross(k));
    CHECK(handle_sdl_event(k, key(SDL_SCANCODE_Z, false)));
    CHECK(!cross(k));
}

TEST(shortcuts_stay_with_the_window) {
    KeyPad k;
    CHECK(!handle_sdl_event(k, key(SDL_SCANCODE_Z, true, SDL_KMOD_LCTRL)));   // Ctrl+Z: the viewer's
    CHECK(!cross(k));
    CHECK(!handle_sdl_event(k, key(SDL_SCANCODE_Z, true, SDL_KMOD_LGUI)));    // Cmd+Z (macOS)
    CHECK(handle_sdl_event(k, key(SDL_SCANCODE_F11, true)));                  // a key like any other (rebind2)
    CHECK(!handle_sdl_event(k, key(SDL_SCANCODE_RETURN, true, SDL_KMOD_LALT)));
    CHECK(handle_sdl_event(k, key(SDL_SCANCODE_RETURN, true)));               // Return alone: Cross (confirm; mac4)
    CHECK(cross(k));
}

TEST(a_bound_modifier_is_a_game_key) {   // rebind2: Ctrl can be bound; then a key pressed with it reaches the game too
    KeyPad k({KeyBinding{SDL_SCANCODE_LCTRL, Ds2::Cross, KeyCtx::Any}});
    CHECK(handle_sdl_event(k, key(SDL_SCANCODE_LCTRL, true, SDL_KMOD_LCTRL)));   // the press carries its own modifier
    CHECK(cross(k));
    CHECK(shortcut_modifiers(KeyPad(), SDL_KMOD_LCTRL) && !shortcut_modifiers(k, SDL_KMOD_LCTRL));
    CHECK(shortcut_modifiers(k, SDL_KMOD_LGUI));   // GUI is not bound: still the window's
    CHECK(handle_sdl_event(k, key(SDL_SCANCODE_LCTRL, false)));
    CHECK(!cross(k));
}

// robust2: player 2 plays with Right Ctrl: holding it does not make player 1's keys window shortcuts
TEST(the_other_players_bound_ctrl_is_a_game_key) {
    KeyPad p1;   // the defaults (Z: Cross)
    KeyPad p2({KeyBinding{SDL_SCANCODE_RCTRL, Ds2::Cross, KeyCtx::Any}});
    CHECK(!handle_sdl_event(p1, key(SDL_SCANCODE_Z, true, SDL_KMOD_RCTRL)));          // alone: a shortcut, as before
    CHECK(!cross(p1));
    CHECK(handle_sdl_event(p1, key(SDL_SCANCODE_Z, true, SDL_KMOD_RCTRL), &p2));      // with player 2's map: the game's
    CHECK(cross(p1));
    CHECK(!shortcut_modifiers(p1, SDL_KMOD_RCTRL, &p2) && shortcut_modifiers(p1, SDL_KMOD_LGUI, &p2));
    CHECK(!handle_sdl_event(p1, key(SDL_SCANCODE_X, true, SDL_KMOD_LGUI), &p2));      // Cmd: still the window's
}

TEST(focus_loss_releases) {
    KeyPad k;
    handle_sdl_event(k, key(SDL_SCANCODE_Z, true));
    SDL_Event f{};
    f.type = SDL_EVENT_WINDOW_FOCUS_LOST;
    CHECK(handle_sdl_event(k, f));
    CHECK(k.sample() == neutral_sample());
    SDL_Event other{};
    other.type = SDL_EVENT_MOUSE_MOTION;
    CHECK(!handle_sdl_event(k, other));
}

TEST_MAIN("SDL3 events -> keyboard pad (src/native/input_keys_sdl.h)")

// A Cmd / Ctrl left set by the OS (macOS screenshot shortcuts) must not kill the F-key shortcuts for the run
TEST(function_keys_ignore_a_held_modifier) {
    CHECK(function_key_ignores_mods(SDL_SCANCODE_F12));
    CHECK(function_key_ignores_mods(SDL_SCANCODE_F1));
    CHECK(function_key_ignores_mods(SDL_SCANCODE_F24));
    CHECK(!function_key_ignores_mods(SDL_SCANCODE_Z));
    CHECK(!function_key_ignores_mods(SDL_SCANCODE_RETURN));
}
