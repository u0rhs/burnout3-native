// The disc's cars and routes for `car list` / `car export` / `track list` / `track export` (include/b3modtool/export3d.h).
// The player cars and their names are the launcher showroom's (src/b3launcher/showroom.h: DATA/GLOBALUS.BIN).
#include <algorithm>
#include <cstdio>

#include "../b3launcher/showroom.h"
#include "b3asset/discfs.h"
#include "b3mod/mods.h"
#include "b3modtool/export3d.h"

namespace b3modtool {

namespace disc = b3asset::disc;

std::vector<CarListEntry> car_list(const std::string& game, bool traffic, std::string* note) {
    std::vector<CarListEntry> out;
    int i = 0;
    for (const auto& c : b3launcher::showroom::list_cars(b3mod::path_from_utf8(game), note)) {
        CarListEntry e;
        e.index = i++;
        e.key = "PVEH/" + c.folder + "/CAR" + std::to_string(c.number) + ".BGV";
        e.path = b3mod::path_utf8(c.file);
        e.name = c.name;
        e.kind = c.kind;
        out.push_back(std::move(e));
    }
    if (!traffic) return out;
    const auto pveh = disc::resolve_ci(b3mod::path_from_utf8(game), "PVEH");
    if (pveh.empty()) return out;
    std::vector<CarListEntry> t;
    for (const disc::Entry& d : disc::list_dir(pveh)) {
        if (!d.dir) continue;
        for (const disc::Entry& f : disc::list_dir(d.path)) {
            const std::string u = to_upper_ascii(f.name);
            if (f.dir || u.size() < 5 || u.substr(u.size() - 4) != ".BTV") continue;
            CarListEntry e;
            e.index = -1;
            e.key = "PVEH/" + to_upper_ascii(d.name) + "/" + u;
            e.path = b3mod::path_utf8(f.path);
            e.kind = "Traffic";
            t.push_back(std::move(e));
        }
    }
    std::sort(t.begin(), t.end(), [](const CarListEntry& a, const CarListEntry& b) { return a.key < b.key; });
    out.insert(out.end(), t.begin(), t.end());
    return out;
}

std::vector<RouteEntry> route_list(const std::string& game) {
    std::vector<RouteEntry> out;
    const auto tracks = disc::resolve_ci(b3mod::path_from_utf8(game), "TRACKS");
    if (tracks.empty()) return out;
    for (const disc::Entry& a : disc::list_dir(tracks)) {
        if (!a.dir) continue;
        for (const disc::Entry& r : disc::list_dir(a.path)) {
            if (!r.dir) continue;
            const auto st = disc::resolve_ci(r.path, "STATIC.DAT");
            if (st.empty()) continue;
            RouteEntry e;
            e.key = "TRACKS/" + to_upper_ascii(a.name) + "/" + to_upper_ascii(r.name);
            e.dir = b3mod::path_utf8(r.path);
            e.static_path = b3mod::path_utf8(st);
            const auto sm = disc::resolve_ci(r.path, "STREAMED.DAT"), en = disc::resolve_ci(r.path, "ENVIRO.DAT");
            if (!sm.empty()) e.streamed_path = b3mod::path_utf8(sm);
            if (!en.empty()) e.enviro_path = b3mod::path_utf8(en);
            out.push_back(std::move(e));
        }
    }
    std::sort(out.begin(), out.end(), [](const RouteEntry& a, const RouteEntry& b) { return a.key < b.key; });
    return out;
}

}  // namespace b3modtool
