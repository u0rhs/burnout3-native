// now_playing_popup.h -- another music app's song in the game's EA TRAX popup (remaster-features §6.2, "Other music
// apps"; 2026-11-13). Host side: src/native/now_playing.h (the system readers, the poller, the popup's decisions).
//
// The display [C decomp]: D_01ECDE90, the EA TRAX display (CB3EATraxDisplay [G]). func_00438F10(display, title, artist,
// album) stores the three UTF-16 pointers at +0x40 / +0x44 / +0x48 and 5.0 at +0x3C (seconds to show); the main loop
// (func_00132600) calls its render func_00438F30 once per frame in every state, whose update func_00438FB0 counts +0x3C
// down by the game's frame time (D_004F50A0) and clears the pointers at 0, and fades the text object (+0x34 -> +0x20:
// +0x8 / +0x4 / +0xC the pointers it draws, +0x10 the fade) in and out. The game's own song popups come from the music
// manager (func_003FC700, tools/native/custom_soundtrack.h).
//
// What this adds: func_00438F30 wrapped (port layer). Each frame the wrapper asks PopupDecider (now_playing.h) with the
// track latched for this VBlank (NowPlayingSource below) and whether the display is free (+0x40 zero and the text
// object's +0x28 zero: nothing shown or fading); on a new song it writes the title / artist / album (the album, else the
// player's name) as UTF-16 into free guest RAM 0xC0F00..0xC1200 (the mod arena's port region GuestArena::kNowPlaying,
// checked zero before the first write; Latin-1 kept, other characters '?', 48 characters at most: custom_music.h
// popup_text) and calls func_00438F10's own row (the table's generated entry, under any port wrapper: the custom
// soundtrack's would put its song's texts in) with them, so the game draws its own popup, its 5 s and its fades. With
// nothing latched (the option off, nothing playing) the wrapper only calls the render: guest RAM untouched.
//
// Presentation only: nothing the simulation reads changes, the pad report is untouched. Determinism: the poller's
// result reaches the game only through NowPlayingSource::latch at a VBlank; native_play records each change of it in a
// replay ("now_playing" setting op) and a replay plays the recorded songs instead of reading the system, so a replay
// shows the same popups on the same frames.
//
// Settings: PC SETTINGS > AUDIO > MUSIC > OTHER MUSIC APPS (ini `other_music_apps` 0-1, default 0; b3r_other_music_apps).
// B3NATIVE_NOW_PLAYING=0/1 overrides it, B3NATIVE_NOW_PLAYING_FILE=<file> reads key=value lines from a file instead of
// the system (tests, scripts), B3NATIVE_NOW_PLAYING_AT=<VBlank> reads nothing before that VBlank (tests: a popup on a
// known frame), B3NATIVE_NOW_PLAYING_OFF=1 does not install the wrapper.
#ifndef B3NATIVE_NOW_PLAYING_POPUP_H
#define B3NATIVE_NOW_PLAYING_POPUP_H

#include "custom_music.h"
#include "hook_table.h"
#include "now_playing.h"
#include "b3port/fileio.h"   // fsrobust
#include "platform.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

namespace b3native {

// The track the game sees, latched once per VBlank from the poller (live, recording) or set from a replay.
class NowPlayingSource {
public:
    // The setting for this VBlank; returns the track from this VBlank on. Off: the poller (if one was made) stops
    // reading and releases its provider; none is ever made while the option stays off. hold: read, but nothing latched
    // yet (B3NATIVE_NOW_PLAYING_AT).
    const b3nowplaying::Track& latch(bool on, std::uint64_t vblank, bool hold = false) {
        if (on && !poller_) {
            poller_ = std::make_unique<b3nowplaying::Poller>([](std::string* why) -> std::unique_ptr<b3nowplaying::Provider> {
                if (const auto f = b3io::getenv("B3NATIVE_NOW_PLAYING_FILE")) return b3nowplaying::file_provider(*f);
                return b3nowplaying::system_provider(why);
            });
            std::printf("now_playing: VBlank %llu: reading other music apps (%s)\n", static_cast<unsigned long long>(vblank),
                        std::getenv("B3NATIVE_NOW_PLAYING_FILE") ? "a file"
                        : b3nowplaying::system_interface() ? b3nowplaying::system_interface() : "no interface on this system");
        }
        b3nowplaying::Track t;
        std::string line;   // the settings row's "Now:" text
        if (poller_) {
            poller_->set_enabled(on);
            if (on) {
                const b3nowplaying::Poller::Snapshot s = poller_->snapshot();
                t = s.track;
                if (!s.ok && s.why != why_ && s.why != "off") {
                    why_ = s.why;
                    std::printf("now_playing: VBlank %llu: %s\n", static_cast<unsigned long long>(vblank), s.why.c_str());
                }
                if (s.ok) why_.clear();
                line = s.ok ? b3nowplaying::describe(t) : s.why.empty() || s.why == "off" ? std::string("looking") : s.why;
            }
        }
        if (line != line_) {
            line_ = line;
            ++line_changes_;
        }
        set(hold ? b3nowplaying::Track{} : t, vblank);
        return track_;
    }
    const std::string& line() const { return line_; }
    std::uint64_t line_changes() const { return line_changes_; }
    void set(const b3nowplaying::Track& t, std::uint64_t vblank) {
        if (t == track_) return;
        track_ = t;
        std::printf("now_playing: VBlank %llu: %s\n", static_cast<unsigned long long>(vblank), b3nowplaying::describe(t).c_str());
    }
    const b3nowplaying::Track& track() const { return track_; }
    bool polling() const { return poller_ != nullptr; }

private:
    std::unique_ptr<b3nowplaying::Poller> poller_;
    b3nowplaying::Track track_;
    std::string why_, line_;
    std::uint64_t line_changes_ = 0;
};

class NowPlayingPopup {
public:
    using TrackFn = const b3nowplaying::Track& (*)();   // the latched track (game thread)
    static constexpr b3host::gaddr kRender = 0x00438F30u, kShow = 0x00438F10u, kDisplay = 0x01ECDE90u;
    static constexpr b3host::gaddr kText = GuestArena::kNowPlaying.address,
                                   kTextEnd = GuestArena::kNowPlaying.address + GuestArena::kNowPlaying.size;
    static_assert(kText == 0x000C0F00u && kTextEnd == 0x000C1200u, "the popup texts stay where they are");
    static constexpr std::size_t kTextUnits = 0x100 / 2 - 1;   // per line, with its terminator
    static constexpr std::size_t kShownUnits = 48;             // as the custom soundtrack's (the game's longest is 40)

    bool install(HookTable& hooks, TrackFn latched) {
        if (!hooks.has_row(kRender) || !hooks.has_row(kShow)) {
            std::fprintf(stderr, "now_playing: the EA TRAX display is not native: no other music apps' popups\n");
            return false;
        }
        show_ = hooks.generated(kShow);
        std::string error;
        if (!hooks.install("port:now_playing", {{kRender, HookLayer::kPort, &render, &render_}}, &error)) {
            std::fprintf(stderr, "now_playing: %s: no other music apps' popups\n", error.c_str());
            return false;
        }
        latched_ = latched;
        instance() = this;
        return true;
    }
    ~NowPlayingPopup() {
        if (instance() != this) return;
        instance() = nullptr;
        if (popups_) std::printf("now_playing: %llu popups of other music apps\n", static_cast<unsigned long long>(popups_));
    }
    std::uint64_t popups() const { return popups_; }

private:
    static NowPlayingPopup*& instance() { static NowPlayingPopup* p = nullptr; return p; }

    static bool display_free(b3host::gaddr display) {
        if (b3host::load<std::uint32_t>(display + 0x40u) != 0) return false;
        const std::uint32_t text = b3host::load<std::uint32_t>(display + 0x34u);
        if (text == 0 || text >= 0x02000000u) return false;   // not set up (or not EE RAM): never touched
        return b3host::load<std::uint32_t>(text + 0x28u) == 0;
    }

    bool write_text(b3host::gaddr at, const std::string& utf8) {
        if (!text_checked_) {   // free guest RAM, once: nothing there yet
            text_checked_ = true;
            text_ok_ = true;
            for (b3host::gaddr k = kText; k < kTextEnd; k += 4)
                if (b3host::load<std::uint32_t>(k) != 0) {
                    std::fprintf(stderr, "now_playing: guest RAM %08X is in use: no other music apps' popups\n", k);
                    text_ok_ = false;
                    break;
                }
        }
        if (!text_ok_) return false;
        const std::u16string u = b3music::popup_text(utf8, kShownUnits);
        b3host::gaddr p = at;
        for (std::size_t k = 0; k < u.size() && k < kTextUnits; ++k, p += 2) b3host::store<std::uint16_t>(p, std::uint16_t(u[k]));
        b3host::store<std::uint16_t>(p, 0);
        return true;
    }

    // func_00438F30(display): the EA TRAX display's render, once per frame
    static void render(const b3host::CallArgs& a, b3host::CallResult& r) {
        NowPlayingPopup* p = instance();
        const b3nowplaying::Track& t = p->latched_();
        if (t.status != b3nowplaying::Status::kNone || p->decider_.pending()) {
            const b3host::gaddr display = a.gpr[0].w[0];
            if (display == kDisplay) {
                if (const auto lines = p->decider_.frame(t, display_free(display))) p->show(a, *lines);
            }
        } else {
            p->decider_.frame(t, false);   // nothing playing: forget the last song
        }
        p->render_(a, r);
    }

    void show(const b3host::CallArgs& a, const b3nowplaying::PopupLines& l) {
        const b3host::gaddr title = kText, artist = kText + 0x100u, album = kText + 0x200u;
        if (!write_text(title, l.title) || !write_text(artist, l.artist) || !write_text(album, l.album)) return;
        b3host::CallArgs b = a;
        b.gpr[1] = u_long128{};
        b.gpr[1].w[0] = title;
        b.gpr[2] = u_long128{};
        b.gpr[2].w[0] = artist;
        b.gpr[3] = u_long128{};
        b.gpr[3].w[0] = album;
        b3host::CallResult r{};
        show_(b, r);
        ++popups_;
        std::printf("now_playing: VBlank %llu: popup: \"%s\" / \"%s\" / \"%s\"\n", static_cast<unsigned long long>(platform::vblank_count()),
                    l.title.c_str(), l.artist.c_str(), l.album.c_str());
        std::fflush(stdout);
    }

    HookEntry render_ = nullptr, show_ = nullptr;
    TrackFn latched_ = nullptr;
    b3nowplaying::PopupDecider decider_;
    bool text_checked_ = false, text_ok_ = false;
    std::uint64_t popups_ = 0;
};

}  // namespace b3native

#endif
