// now_playing_mpris.cpp -- the Linux reader of src/native/now_playing.h: MPRIS (the Media Player Remote Interfacing
// Specification, v2.2) on the session D-Bus, through libdbus-1 loaded at run time (src/native/dbus_dl.h).
//
// Each read: org.freedesktop.DBus.ListNames; for every org.mpris.MediaPlayer2.<player> name (in name order),
// org.freedesktop.DBus.Properties.Get of org.mpris.MediaPlayer2.Player PlaybackStatus ("Playing" / "Paused" /
// "Stopped"); the player shown is the first that plays, else the first paused one (choose_player); of it, Metadata
// (a{sv}: xesam:title s, xesam:artist as, xesam:album s) and, once per name, org.mpris.MediaPlayer2 Identity (the
// player's name, "Spotify"). Every call waits at most 500 ms. A player that does not answer is left out.
#include "now_playing.h"

#if defined(__linux__)
#include "dbus_dl.h"

#include <algorithm>
#include <map>

namespace b3nowplaying {

namespace {

using namespace b3dbus;

constexpr int kTimeoutMs = 500;
constexpr const char* kPrefix = "org.mpris.MediaPlayer2.";

class MprisProvider : public Provider {
public:
    MprisProvider(Lib& lib, DBusConnection* c) : d_(lib), c_(c) {}
    ~MprisProvider() override { d_.end(c_); }
    const char* name() const override { return "MPRIS"; }
    bool read(Track& out, std::string* why) override;

private:
    // a method call with string arguments; the reply (unref it) or null and *why
    DBusMessage* call(const char* dest, const char* path, const char* iface, const char* method, const char* a1, const char* a2,
                      std::string* why);
    DBusMessage* get_property(const std::string& dest, const char* iface, const char* prop, std::string* why) {
        return call(dest.c_str(), "/org/mpris/MediaPlayer2", "org.freedesktop.DBus.Properties", "Get", iface, prop, why);
    }
    // the variant at the start of a Get reply, entered: false if it is not one
    bool variant(DBusMessage* m, DBusMessageIter& inner) {
        DBusMessageIter it;
        if (!d_.message_iter_init(m, &it) || d_.message_iter_get_arg_type(&it) != kTypeVariant) return false;
        d_.message_iter_recurse(&it, &inner);
        return true;
    }
    std::string string_at(DBusMessageIter& it) {
        const int t = d_.message_iter_get_arg_type(&it);
        if (t != kTypeString && t != kTypeObjectPath) return {};
        const char* s = nullptr;
        d_.message_iter_get_basic(&it, &s);
        return s ? std::string(s) : std::string();
    }
    void metadata(const std::string& dest, Track& t);
    std::string identity(const std::string& dest);

    Lib& d_;
    DBusConnection* c_;
    std::map<std::string, std::string> identities_;   // bus name -> Identity
};

DBusMessage* MprisProvider::call(const char* dest, const char* path, const char* iface, const char* method, const char* a1, const char* a2,
                                 std::string* why) {
    DBusMessage* m = d_.message_new_method_call(dest, path, iface, method);
    if (!m) {
        if (why) *why = "out of memory";
        return nullptr;
    }
    if (a1 && a2) d_.message_append_args(m, kTypeString, &a1, kTypeString, &a2, kTypeInvalid);
    else if (a1) d_.message_append_args(m, kTypeString, &a1, kTypeInvalid);
    DBusError e;
    d_.error_init(&e);
    DBusMessage* r = d_.connection_send_with_reply_and_block(c_, m, kTimeoutMs, &e);
    d_.message_unref(m);
    if (!r) {
        if (why) *why = std::string(method) + " on " + dest + ": " + (e.message ? e.message : "no reply");
        d_.error_free(&e);
        return nullptr;
    }
    d_.error_free(&e);
    return r;
}

void MprisProvider::metadata(const std::string& dest, Track& t) {
    DBusMessage* r = get_property(dest, "org.mpris.MediaPlayer2.Player", "Metadata", nullptr);
    if (!r) return;
    DBusMessageIter v, dict;
    if (variant(r, v) && d_.message_iter_get_arg_type(&v) == kTypeArray) {
        d_.message_iter_recurse(&v, &dict);
        for (; d_.message_iter_get_arg_type(&dict) == kTypeDictEntry; d_.message_iter_next(&dict)) {
            DBusMessageIter entry, val, inner;
            d_.message_iter_recurse(&dict, &entry);
            const std::string key = string_at(entry);
            if (!d_.message_iter_next(&entry) || d_.message_iter_get_arg_type(&entry) != kTypeVariant) continue;
            d_.message_iter_recurse(&entry, &val);
            if (key == "xesam:title") t.title = string_at(val);
            else if (key == "xesam:album") t.album = string_at(val);
            else if (key == "xesam:artist") {   // as (some players send one s)
                if (d_.message_iter_get_arg_type(&val) == kTypeArray) {
                    std::string all;
                    d_.message_iter_recurse(&val, &inner);
                    for (; d_.message_iter_get_arg_type(&inner) == kTypeString; d_.message_iter_next(&inner)) {
                        const std::string a = string_at(inner);
                        if (a.empty()) continue;
                        if (!all.empty()) all += ", ";
                        all += a;
                    }
                    t.artist = all;
                } else {
                    t.artist = string_at(val);
                }
            }
        }
    }
    d_.message_unref(r);
}

std::string MprisProvider::identity(const std::string& dest) {
    if (auto it = identities_.find(dest); it != identities_.end()) return it->second;
    std::string id;
    if (DBusMessage* r = get_property(dest, "org.mpris.MediaPlayer2", "Identity", nullptr)) {
        DBusMessageIter v;
        if (variant(r, v)) id = string_at(v);
        d_.message_unref(r);
    }
    if (id.empty()) {   // the name's first part: org.mpris.MediaPlayer2.vlc.instance123 -> Vlc
        id = dest.substr(std::string(kPrefix).size());
        if (const std::size_t dot = id.find('.'); dot != std::string::npos) id.resize(dot);
        if (!id.empty() && id[0] >= 'a' && id[0] <= 'z') id[0] = char(id[0] - 'a' + 'A');
    }
    if (identities_.size() > 64) identities_.clear();
    identities_[dest] = id;
    return id;
}

bool MprisProvider::read(Track& out, std::string* why) {
    DBusMessage* r = call("org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "ListNames", nullptr, nullptr, why);
    if (!r) return false;
    std::vector<std::string> names;
    DBusMessageIter it, arr;
    if (d_.message_iter_init(r, &it) && d_.message_iter_get_arg_type(&it) == kTypeArray) {
        d_.message_iter_recurse(&it, &arr);
        for (; d_.message_iter_get_arg_type(&arr) == kTypeString; d_.message_iter_next(&arr)) {
            std::string n = string_at(arr);
            if (n.rfind(kPrefix, 0) == 0) names.push_back(std::move(n));
        }
    }
    d_.message_unref(r);
    std::sort(names.begin(), names.end());
    std::vector<Player> players;
    for (const std::string& n : names) {
        DBusMessage* s = get_property(n, "org.mpris.MediaPlayer2.Player", "PlaybackStatus", nullptr);
        if (!s) continue;
        Player p;
        p.bus_name = n;
        DBusMessageIter v;
        if (variant(s, v)) {
            const std::string st = string_at(v);
            p.track.status = st == "Playing" ? Status::kPlaying : st == "Paused" ? Status::kPaused : Status::kNone;
        }
        d_.message_unref(s);
        players.push_back(std::move(p));
    }
    out = Track{};
    const int k = choose_player(players);
    if (k < 0) return true;
    out.status = players[std::size_t(k)].track.status;
    metadata(players[std::size_t(k)].bus_name, out);
    out.app = identity(players[std::size_t(k)].bus_name);
    return true;
}

Lib& lib() {
    static Lib l;   // loaded once, never unloaded (SDL may use the same library)
    return l;
}

}  // namespace

std::unique_ptr<Provider> mpris_provider(std::string* why) {
    static std::mutex m;
    std::lock_guard<std::mutex> lock(m);
    Lib& l = lib();
    if (!l.load(why)) return nullptr;
    DBusConnection* c = l.session(why);
    if (!c) return nullptr;
    return std::make_unique<MprisProvider>(l, c);
}

}  // namespace b3nowplaying

#else

namespace b3nowplaying {
std::unique_ptr<Provider> mpris_provider(std::string* why) {
    if (why) *why = "MPRIS is a Linux interface";
    return nullptr;
}
}  // namespace b3nowplaying

#endif
