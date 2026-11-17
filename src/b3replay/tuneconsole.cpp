// The tuning console in the native renderer (tune4; tunenav1: the tabs, the car page, favourites, search; tunenav2:
// the CHANGED tab, the markers and counts, RESET SECTION / RESET ALL): src/b3replay/tuneconsole.h.
#include "tuneconsole.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>

#include "b3tune/tune.h"
#include "glyphdraw.h"
#include "nativepad.h"
#include "tunelayout.h"

namespace b3replay {

namespace bw = b3tune::browse;

namespace {

b3tune::Entry entry_of(const B3TuneVar& v) {
    b3tune::Entry e;
    e.type = static_cast<b3tune::Type>(v.type);
    e.count = v.count;
    e.flags = std::uint8_t(v.flags);
    e.min_word = v.min_word;
    e.max_word = v.max_word;
    e.key = v.key;
    e.group = v.group ? v.group : "";
    e.name = v.name ? v.name : "";
    e.file = v.file ? v.file : "";
    return e;
}
// the DS2 report: active-low buttons (pcpage.cpp's order)
bool pressed(const std::array<std::uint8_t, 32>& r, unsigned i) { return (r[i / 8] & (1u << (i % 8))) == 0; }
enum : unsigned { kSelect = 0, kStart = 3, kUp = 4, kRight = 5, kDown = 6, kLeft = 7, kL2 = 8, kR2 = 9, kL1 = 10, kR1 = 11, kTriangle = 12,
                  kCircle = 13, kCross = 14, kSquare = 15 };
// the DEVELOPER tab's ids (between the settings' own rows and kRowTuneFirst's variables)
constexpr std::uint32_t kSectionBase = 0xFFFF2000u;   // its section rows (b3settings: kSection ids)
constexpr std::uint32_t kNavBack = 0x00F00000u, kNavCat = 0x00F00010u, kNavCar = 0x00F00100u, kNavNode = 0x00F01000u;
constexpr std::uint32_t kNavVirtual = 0x00F00F00u;   // + (-page): MOST USED's lists, YOUR CAR (browse.h kPage*)
// tunenav2: RESET ALL (the tab's own row, the CHANGED page's), RESET THIS PAGE
constexpr std::uint32_t kNavResetAll = 0x00F00020u, kNavResetAllRow = 0x00F00021u, kNavResetPage = 0x00F00022u;
constexpr std::uint64_t kAskNs = 6000000000ull;      // the DEVELOPER tab's question stays 6 s
constexpr std::size_t kOverlayRows = 15;
std::uint64_t now_ns() { return SDL_GetTicksNS(); }
std::string plural(std::size_t n, const char* one) { return std::to_string(n) + " " + one + (n == 1 ? "" : "s"); }
std::string upper_text(std::string t) {
    for (char& c : t) c = char(std::toupper(static_cast<unsigned char>(c)));
    return t;
}

}  // namespace

TuneConsole& TuneConsole::instance() {
    static TuneConsole* p = new TuneConsole();   // never destroyed (the render thread may outlive static destruction)
    return *p;
}

void TuneConsole::set_hooks(const B3TuneHooks* h) {
    std::lock_guard<std::mutex> l(mu_);
    hooks_ = h && h->size >= sizeof(B3TuneHooks) && h->count && h->get && h->edit ? h : nullptr;
    gen_seen_ = 0;
}
bool TuneConsole::available() const {
    std::lock_guard<std::mutex> l(mu_);
    return hooks_ != nullptr;
}

void TuneConsole::set_user_dir(const std::string& dir) {
    std::lock_guard<std::mutex> l(mu_);
    std::string path;
    if (const char* e = std::getenv("B3TUNE_FAVOURITES"); e && *e) path = e;
    else if (!dir.empty()) path = (std::filesystem::path(dir) / "tuning_console.ini").string();
    if (path == usage_path_) return;
    usage_path_ = path;
    if (!path.empty() && usage_.load(path)) {
        std::printf("[b3tune] favourites: %zu, recently changed: %zu (%s)\n", usage_.favourites().size(), usage_.recent().size(), path.c_str());
        std::fflush(stdout);
    }
    ov_.refresh();
    pg_.refresh();
}
void TuneConsole::save_usage(bool now) {
    if (!usage_.dirty() || usage_path_.empty()) return;
    const std::uint64_t t = now_ns();
    if (!now && t - usage_saved_ns_ < 1000000000ull) return;
    usage_saved_ns_ = t;
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(usage_path_).parent_path(), ec);
    if (!usage_.save(usage_path_)) std::printf("[b3tune] favourites: cannot write %s\n", usage_path_.c_str());
}

std::vector<B3TuneVar> TuneConsole::live_vars() const {
    std::vector<B3TuneVar> out;
    const B3TuneHooks* h = hooks_;
    if (!h) return out;
    const int n = h->count();
    out.reserve(std::size_t(std::max(n, 0)));
    for (int i = 0; i < n; ++i) {
        B3TuneVar v{};
        if (h->get(i, &v) && v.live) out.push_back(v);
    }
    return out;
}
bool TuneConsole::var(std::uint32_t index, B3TuneVar& out) const {
    out = B3TuneVar{};
    return hooks_ && hooks_->get(int(index), &out) && out.live;
}

void TuneConsole::sync() {
    if (!hooks_) return;
    const std::uint64_t t = now_ns();
    bool rebuild = false;
    if (hooks_->generation) {
        const std::uint32_t g = hooks_->generation();
        rebuild = g != gen_seen_;
        gen_seen_ = g;
    } else if (t - scan_ns_ > 250000000ull || !scan_ns_) {   // an older native_play: look again now and then
        scan_ns_ = t;
        rebuild = true;
    }
    if (rebuild) {
        std::vector<bw::VarInfo> infos;
        for (const B3TuneVar& v : live_vars()) infos.push_back(bw::info_of(v));
        ov_.set_vars(infos);
        pg_.set_vars(infos);
    }
    if (hooks_->car && (t - car_ns_ > 100000000ull || !car_ns_)) {
        car_ns_ = t;
        B3TuneCar c{};
        c.size = sizeof c;
        if (hooks_->car(&c)) car_ = c;
    }
    read_changes();
}

// tunenav2: the changed values (a few at most: only edited ones can be), each time (the views make their rows again
// only when the list differs)
void TuneConsole::read_changes() {
    std::vector<bw::Change> cs;
    change_of_.clear();
    if (hooks_->changes) {
        int n = hooks_->changes(change_buf_.data(), int(change_buf_.size()));
        if (n > int(change_buf_.size())) {
            change_buf_.resize(std::size_t(n) + 16);
            n = hooks_->changes(change_buf_.data(), int(change_buf_.size()));
        }
        for (int k = 0; k < n && k < int(change_buf_.size()); ++k) {
            const B3TuneChange& c = change_buf_[std::size_t(k)];
            change_of_[{c.index, c.element}] = c;
            bw::Change ch;
            ch.item = {c.index, c.element};
            ch.live = c.live && ov_.tree().where(c.index, c.element).first >= 0;
            if (!ch.live) {   // not in the tree now: its name for the list
                B3TuneVar v{};
                if (hooks_->get(int(c.index), &v)) {
                    const bw::VarInfo vi = bw::info_of(v);
                    ch.label = bw::element_label(vi, c.element) + " - " + bw::file_stem(vi.file);
                }
            }
            cs.push_back(ch);
        }
    }
    for (int f : {int(b3tune::car::kBoostRate), int(b3tune::car::kFreezeDamage), int(b3tune::car::kInfiniteBoost)})
        if ((car_.changed >> f) & 1u) {
            bw::Change ch;
            ch.car = f;
            cs.push_back(ch);
        }
    ov_.set_changes(cs);
    pg_.set_changes(std::move(cs));
}

std::string TuneConsole::element_value(const B3TuneVar& v, std::uint32_t element) {
    if (element >= B3TUNE_MAX_WORDS) return "?";
    return b3tune::format_word(entry_of(v), v.words[element]);
}

void TuneConsole::step(const B3TuneVar& v, std::uint32_t element, int dir, int mult) {
    if (!hooks_ || element >= v.elements || element >= B3TUNE_MAX_WORDS) return;
    const b3tune::Entry e = entry_of(v);
    const std::uint32_t w = mult < 0 ? b3tune::step_word_fine(e, v.words[element], dir) : b3tune::step_word(e, v.words[element], dir, mult);
    if (w != v.words[element]) {
        hooks_->edit(int(v.index), int(element), w);
        usage_.changed(bw::ident(bw::info_of(v), element));
        save_usage(false);
    }
}
// tunenav2: native_play puts back the default (its own copy of the value the game gave it) at the next VBlank, as an
// edit it records
bool TuneConsole::reset_var(std::uint32_t index, std::uint32_t element) {
    if (!hooks_ || !hooks_->reset || !change_of_.count({index, element})) return false;
    hooks_->reset(int(index), int(element));
    B3TuneVar v{};
    if (hooks_->get(int(index), &v)) {
        const std::string id = bw::ident(bw::info_of(v), element);
        usage_.changed(id);
        save_usage(false);
        std::printf("[b3tune] reset: %s back to its default\n", id.c_str());
        std::fflush(stdout);
    }
    return true;
}
bool TuneConsole::reset_car(int field) {
    if (!hooks_ || !hooks_->car_edit || !((car_.changed >> field) & 1u)) return false;
    namespace c = b3tune::car;
    if (field == int(c::kFreezeDamage) || field == int(c::kInfiniteBoost)) hooks_->car_edit(field, 0);
    else if (field == int(c::kBoostRate)) hooks_->car_edit(field, b3tune::float_word(car_.boost_rate_default));
    else return false;
    car_ns_ = 0;
    std::printf("[b3tune] reset: car %s back to the game's\n", c::field_name(c::Field(field)));
    std::fflush(stdout);
    return true;
}
void TuneConsole::reset_changes(const std::vector<bw::Change>& cs) {
    for (const bw::Change& c : cs) {
        if (c.car) reset_car(c.car);
        else if (hooks_ && hooks_->reset) hooks_->reset(int(c.item.var), int(c.item.element));
    }
}
void TuneConsole::car_change(int field, int dir, int mult) {
    if (!hooks_ || !hooks_->car_edit || !bw::car_editable(field)) return;
    std::uint32_t word = 0;
    if (!bw::car_change(field, car_, dir, mult, word)) return;
    hooks_->car_edit(field, word);
    car_ns_ = 0;   // read back soon
}

std::string TuneConsole::row_value(const bw::Row& r) {
    switch (r.kind) {
    case bw::Row::Kind::kNode: {
        std::size_t n = 0;
        if (r.node >= 0) n = ov_.tree().node(r.node).count;
        else if (r.node == bw::kPageFavourites) n = usage_.favourites().size();
        else if (r.node == bw::kPageRecent) n = usage_.recent().size();
        else if (r.node == bw::kPageStarter) n = bw::starter().size();
        return std::to_string(n) + " >";
    }
    case bw::Row::Kind::kVar: {
        B3TuneVar v{};
        const auto ch = change_of_.find({r.item.var, r.item.element});
        if (!var(r.item.var, v)) {   // not live: a change kept for later shows its words
            if (ch == change_of_.end() || !hooks_->get(int(r.item.var), &v)) return "-";
            const b3tune::Entry e = entry_of(v);
            return b3tune::format_word(e, ch->second.word) + " (default " + b3tune::format_word(e, ch->second.def) + ")";
        }
        std::string t = element_value(v, r.item.element);
        if (ch != change_of_.end()) t += " (default " + b3tune::format_word(entry_of(v), ch->second.def) + ")";
        return t;
    }
    case bw::Row::Kind::kCar: {
        std::string t = bw::car_value(r.car, car_);
        if ((car_.changed >> r.car) & 1u)
            t += r.car == int(b3tune::car::kBoostRate) ? " (default " + bw::fixed(car_.boost_rate_default, 2) + ")" : " (default OFF)";
        return t;
    }
    case bw::Row::Kind::kAction: return plural(ov_.changes().size(), "value");
    case bw::Row::Kind::kText: return "";
    }
    return "";
}
bool TuneConsole::row_changed(const bw::Browser& b, const bw::Row& r) const {
    switch (r.kind) {
    case bw::Row::Kind::kVar: return change_of_.count({r.item.var, r.item.element}) != 0;
    case bw::Row::Kind::kCar: return b.car_changed(r.car);
    case bw::Row::Kind::kNode: return b.changed_count(r.node) > 0;
    case bw::Row::Kind::kAction: return true;
    case bw::Row::Kind::kText: return false;
    }
    return false;
}
// "* " a changed value, "+ " a favourite; a section with changed values "Steering (2)"
std::string TuneConsole::row_label(const bw::Browser& b, const bw::Row& r) {
    std::string label = r.label;
    if (r.kind == bw::Row::Kind::kNode) {
        if (const std::size_t n = b.changed_count(r.node)) label += " (" + std::to_string(n) + ")";
        return label;
    }
    if (r.kind == bw::Row::Kind::kVar) {
        B3TuneVar v{};
        if (hooks_->get(int(r.item.var), &v) && usage_.favourite(bw::ident(bw::info_of(v), r.item.element))) label = "+ " + label;
    }
    if ((r.kind == bw::Row::Kind::kVar || r.kind == bw::Row::Kind::kCar) && row_changed(b, r)) label = "* " + label;
    return label;
}
std::string TuneConsole::row_help(const bw::Row& r) {
    switch (r.kind) {
    case bw::Row::Kind::kNode:
        if (r.node >= 0 && r.node < int(bw::kCats)) return bw::cat_info(bw::Cat(r.node)).about;
        if (r.node >= 0) {
            const bw::Node& n = ov_.tree().node(r.node);
            return std::to_string(n.count) + (n.count == 1 ? " value" : " values") +
                   (n.kids.empty() ? std::string() : " in " + std::to_string(n.kids.size()) + " subsections") + ": RIGHT opens it.";
        }
        return r.node == bw::kPageFavourites ? "The values you pinned (F, a controller: TRIANGLE)."
               : r.node == bw::kPageRecent   ? "The values you changed last, the newest first (a history: CHANGED has what differs now)."
               : r.node == bw::kPageStarter  ? "A few of the most interesting values to begin with."
                                             : "";
    case bw::Row::Kind::kAction:
        return "Every changed value back to its default (" + plural(ov_.changes().size(), "value") + "): it asks first.";
    case bw::Row::Kind::kVar: {
        B3TuneVar v{};
        if (!var(r.item.var, v))
            return change_of_.count({r.item.var, r.item.element}) ? "Not registered now: the change is kept for when it is (the next race)."
                                                                  : "Not registered now.";
        const b3tune::Entry e = entry_of(v);
        std::string d = std::string(v.group ? v.group : "") + " | " + (v.name ? v.name : "") + "  (" + bw::file_stem(v.file ? v.file : "") + ", " +
                        e.type_name();
        if (e.ranged()) d += ", " + b3tune::format_word(e, v.min_word) + " to " + b3tune::format_word(e, v.max_word);
        d += ")";
        if (r.elsewhere) d += "  ENTER: go to its place";
        return d;
    }
    case bw::Row::Kind::kCar:
        for (const bw::CarRow& c : bw::car_rows())
            if (c.field == r.car) return std::string(c.help) + (r.elsewhere ? "  ENTER: go to it" : "");
        return "";
    case bw::Row::Kind::kText: return "";
    }
    return "";
}

// ---- PC SETTINGS > DEVELOPER ----
std::vector<b3settings::Tab> TuneConsole::tabs(bool developer_mods) {
    std::lock_guard<std::mutex> l(mu_);
    enabled_ = developer_mods;
    std::vector<b3settings::Tab> out;
    if (developer_mods && !said_) {   // once: whether the tab is there (the log of a bug report)
        said_ = true;
        std::printf("[b3tune] DEVELOPER MODS on: %s\n", hooks_ ? "the DEVELOPER tab and F9 are there"
                                                                : "no tuning console in this run (it is installed at the start: start again)");
        std::fflush(stdout);
    }
    if (!developer_mods || !hooks_) return out;
    sync();
    save_usage(false);
    b3settings::Tab t;
    t.label = "DEVELOPER";
    {
        b3settings::Row r;
        r.id = b3settings::kActionTuneSavePreset;
        r.kind = b3settings::RowKind::kAction;
        r.label = "SAVE AS PRESET";
        r.values = {"Save"};
        r.description = "Writes this session's changes as a new mod (a tuning preset) in your mods folder, off until you turn "
                        "it on in the launcher's Mods page." + (status_.empty() ? std::string() : " " + status_);
        t.rows.push_back(r);
    }
    {
        char st[512] = {};
        if (hooks_->status) hooks_->status(st, int(sizeof st));
        b3settings::Row r;
        r.id = b3settings::kActionTuneOverlay;
        r.kind = b3settings::RowKind::kAction;
        r.label = "LIVE OVERLAY";
        const std::string key = NativePad::instance().port_key_text(b3native::input::PortAction::TuneOverlay);
        r.values = {key};
        r.description = std::string(st) + " In a race, " + key + " shows the same pages over the game (with search and favourites). "
                        "Changes are written at the next frame of the game and kept in its replays; they change gameplay.";
        r.disabled = true;
        t.rows.push_back(r);
    }
    // tunenav2: RESET ALL (asks: a second CONFIRM within 6 s)
    if (pg_ask_ != Ask::kNone && now_ns() - pg_ask_ns_ > kAskNs) pg_ask_ = Ask::kNone;
    const std::size_t nchanged = pg_.changes().size();
    auto reset_all_row = [&](std::uint32_t id) {
        b3settings::Row r;
        r.id = id;
        r.kind = b3settings::RowKind::kAction;
        if (pg_ask_ == Ask::kAll) {
            r.label = "RESET ALL: SURE?";
            r.values = {"CONFIRM AGAIN"};
            r.description = "CONFIRM again puts back all " + plural(nchanged, "changed value") +
                            " to the default; anything else (or 6 seconds) cancels.";
        } else {
            r.label = "RESET ALL";
            r.values = {nchanged ? plural(nchanged, "value") : std::string("Nothing changed")};
            r.description = nchanged ? "Puts back every value that differs from its default (the CHANGED category lists them), and the car "
                                       "page's FREEZE DAMAGE, INFINITE BOOST and boost use rate. CONFIRM asks first."
                                     : "Nothing changed: every value is the game's own.";
            r.disabled = nchanged == 0;
        }
        if (!pg_note_.empty()) r.description += " " + pg_note_;
        return r;
    };
    t.rows.push_back(reset_all_row(kNavResetAll));
    auto section = [&](std::uint32_t k, const std::string& label) {
        b3settings::Row s;
        s.id = kSectionBase + k;
        s.kind = b3settings::RowKind::kSection;
        s.label = label;
        t.rows.push_back(s);
    };
    if (!pg_in_) {   // the category list
        section(0, "CATEGORIES");
        for (std::size_t c = 0; c < bw::kCats; ++c) {
            b3settings::Row r;
            r.id = kNavCat + std::uint32_t(c);
            r.kind = b3settings::RowKind::kAction;
            r.label = bw::cat_info(bw::Cat(c)).tab;
            const std::size_t ch = pg_.tab_changed(c);   // tunenav2: "HANDLING (3)"
            if (ch && c != std::size_t(bw::Cat::kChanged)) r.label += " (" + std::to_string(ch) + ")";
            const std::size_t n = c == std::size_t(bw::Cat::kChanged) ? ch : c >= std::size_t(bw::Cat::kHandling) ? pg_.tree().node(int(c)).count : 0;
            r.values = {n ? std::to_string(n) + " >" : ">"};
            r.description = std::string(bw::cat_info(bw::Cat(c)).about) + " CONFIRM or RIGHT opens it.";
            if (ch) r.description += " " + plural(ch, "value") + " changed here.";
            t.rows.push_back(r);
        }
        out.push_back(t);
        return out;
    }
    section(1, pg_.crumbs());
    {
        b3settings::Row r;
        r.id = kNavBack;
        r.kind = b3settings::RowKind::kAction;
        r.label = "BACK";
        r.values = {"<"};
        r.description = pg_.depth() > 0 ? "Up one level (LEFT on a section row goes up too)." : "Back to the categories.";
        t.rows.push_back(r);
    }
    if (const std::size_t here = pg_.section_changes().size(); here && pg_.page() != bw::kPageChanged) {   // tunenav2
        b3settings::Row r;
        r.id = kNavResetPage;
        r.kind = b3settings::RowKind::kAction;
        if (pg_ask_ == Ask::kSection) {
            r.label = "RESET THIS PAGE: SURE?";
            r.values = {"CONFIRM AGAIN"};
            r.description = "CONFIRM again puts back the " + plural(here, "changed value") + " of this page (its subsections too); "
                            "anything else (or 6 seconds) cancels.";
        } else {
            r.label = "RESET THIS PAGE";
            r.values = {plural(here, "value")};
            r.description = "Puts back the changed values of this page and its subsections to their defaults" +
                            std::string(here > 1 ? " (CONFIRM asks first)." : ".");
        }
        t.rows.push_back(r);
    }
    std::uint32_t texts = 2;
    for (const bw::Row& row : pg_.rows()) {
        b3settings::Row r;
        r.kind = b3settings::RowKind::kAction;
        r.label = row_label(pg_, row);
        r.values = {row_value(row)};
        r.description = row_help(row);
        switch (row.kind) {
        case bw::Row::Kind::kText:
            section(texts++, row.label);
            continue;
        case bw::Row::Kind::kAction:
            t.rows.push_back(reset_all_row(kNavResetAllRow));
            continue;
        case bw::Row::Kind::kNode:
            r.id = row.node >= 0 ? kNavNode + std::uint32_t(row.node) : kNavVirtual + std::uint32_t(-row.node);
            r.description += (r.description.empty() ? "" : " ") + std::string("CONFIRM or RIGHT opens it.");
            break;
        case bw::Row::Kind::kVar: {
            r.id = b3settings::kRowTuneFirst + row.item.var * 64u + row.item.element;
            r.description += ". LEFT / RIGHT change it; CONFIRM puts back its default.";
            break;
        }
        case bw::Row::Kind::kCar:
            r.id = kNavCar + std::uint32_t(row.car);
            r.disabled = !bw::car_editable(row.car);
            if (row.car == int(b3tune::car::kBoostRate) && pg_.car_changed(row.car)) r.description += " CONFIRM puts back the game's.";
            break;
        }
        t.rows.push_back(r);
    }
    out.push_back(t);
    return out;
}

bool TuneConsole::edit(const b3settings::Edit& e) {
    std::lock_guard<std::mutex> l(mu_);
    if (e.id == b3settings::kActionTuneSavePreset) {
        if (hooks_ && hooks_->save_preset) {
            char msg[512] = {};
            hooks_->save_preset(msg, int(sizeof msg));
            status_ = msg;
        }
        return true;
    }
    if (e.id == b3settings::kActionTuneOverlay) return true;
    if (!hooks_) return false;
    // tunenav2: RESET ALL / RESET THIS PAGE: the first CONFIRM asks, a second within 6 s does it; any other edit cancels
    const bool reset_row = e.id == kNavResetAll || e.id == kNavResetAllRow || e.id == kNavResetPage;
    if (!reset_row) {
        pg_ask_ = Ask::kNone;
        pg_note_.clear();
    }
    if (reset_row) {
        if (e.direction != 0) return true;
        sync();
        const Ask what = e.id == kNavResetPage ? Ask::kSection : Ask::kAll;
        const std::vector<bw::Change> cs = what == Ask::kAll ? pg_.changes() : pg_.section_changes();
        if (cs.empty()) { pg_ask_ = Ask::kNone; return true; }
        if (pg_ask_ != what && cs.size() > 1) {
            pg_ask_ = what;
            pg_ask_ns_ = now_ns();
            pg_note_.clear();
            std::printf("[b3tune] DEVELOPER: %s: asked (%zu values)\n", what == Ask::kAll ? "reset all" : "reset this page", cs.size());
            std::fflush(stdout);
            return true;
        }
        if (what == Ask::kAll && hooks_->reset) hooks_->reset(-1, -1);
        else reset_changes(cs);
        pg_ask_ = Ask::kNone;
        pg_note_ = plural(cs.size(), "value") + " put back to the default.";
        std::printf("[b3tune] DEVELOPER: %s: %zu values back to their defaults\n", what == Ask::kAll ? "reset all" : "reset this page", cs.size());
        std::fflush(stdout);
        return true;
    }
    auto up = [&] { if (!pg_.back()) pg_in_ = false; };
    if (e.id >= kNavCat && e.id < kNavCat + bw::kCats) {
        pg_.set_tab(e.id - kNavCat);
        pg_in_ = true;
        return true;
    }
    if (e.id == kNavBack) { up(); return true; }
    auto open_row = [&](auto pred) {
        const std::vector<bw::Row>& rs = pg_.rows();
        for (std::size_t i = 0; i < rs.size(); ++i)
            if (pred(rs[i])) {
                pg_.set_cursor(i);
                pg_.enter();
                return;
            }
    };
    if (e.id >= kNavNode && e.id < b3settings::kRowTuneFirst) {
        if (e.direction < 0) { up(); return true; }
        const int node = int(e.id - kNavNode);
        open_row([&](const bw::Row& r) { return r.kind == bw::Row::Kind::kNode && r.node == node; });
        return true;
    }
    if (e.id > kNavVirtual && e.id < kNavNode) {
        if (e.direction < 0) { up(); return true; }
        const int node = -int(e.id - kNavVirtual);
        open_row([&](const bw::Row& r) { return r.kind == bw::Row::Kind::kNode && r.node == node; });
        return true;
    }
    if (e.id >= kNavCar && e.id < kNavVirtual) {
        sync();
        const int field = int(e.id - kNavCar);
        if (e.direction == 0 && field == int(b3tune::car::kBoostRate)) reset_car(field);   // tunenav2: CONFIRM: the game's rate
        else car_change(field, e.direction, 1);
        return true;
    }
    if (e.id < b3settings::kRowTuneFirst) return false;
    const std::uint32_t idx = (e.id - b3settings::kRowTuneFirst) / 64u, el = (e.id - b3settings::kRowTuneFirst) % 64u;
    sync();
    if (e.direction == 0) {   // CONFIRM: its default (a change kept for a variable not registered now too)
        reset_var(idx, el);
        return true;
    }
    B3TuneVar v{};
    if (!hooks_->get(int(idx), &v) || !v.live) return true;
    step(v, el, e.direction, 1);
    return true;
}

// ---- the F9 overlay ----
void TuneConsole::close() {
    if (open_) save_usage(true);
    if (ask_ != Ask::kNone) answer(false);   // a question left open is a no
    open_ = false;
}

void TuneConsole::change(int dir, int mult) {
    const bw::Row* r = ov_.current();
    if (!r) return;
    switch (r->kind) {
    case bw::Row::Kind::kNode:
        if (dir > 0) ov_.enter();
        else ov_.back();
        return;
    case bw::Row::Kind::kText:
        if (dir < 0) ov_.back();
        return;
    case bw::Row::Kind::kVar: {
        B3TuneVar v{};
        if (var(r->item.var, v)) step(v, r->item.element, dir, mult);
        return;
    }
    case bw::Row::Kind::kCar: car_change(r->car, dir, mult); return;
    case bw::Row::Kind::kAction: return;
    }
}
void TuneConsole::confirm() {
    const bw::Row* r = ov_.current();
    if (!r) return;
    if (r->kind == bw::Row::Kind::kAction) {   // tunenav2: the CHANGED page's RESET ALL
        ask(Ask::kAll);
        return;
    }
    if (r->kind == bw::Row::Kind::kCar && !r->elsewhere) {
        if (r->car == int(b3tune::car::kRepair) || r->car == int(b3tune::car::kFillBoost) || r->car == int(b3tune::car::kFreezeDamage) ||
            r->car == int(b3tune::car::kInfiniteBoost))
            car_change(r->car, 0, 1);
        return;
    }
    ov_.enter();
}
void TuneConsole::go_back() {
    if (!ov_.back()) close();
}
void TuneConsole::favourite() {
    const bw::Row* r = ov_.current();
    if (!r || r->kind != bw::Row::Kind::kVar) return;
    B3TuneVar v{};
    if (!var(r->item.var, v)) return;
    const bool on = usage_.toggle(bw::ident(bw::info_of(v), r->item.element));
    std::printf("[b3tune] %s %s\n", on ? "favourite:" : "no longer a favourite:", bw::ident(bw::info_of(v), r->item.element).c_str());
    std::fflush(stdout);
    save_usage(true);
    if (ov_.page() == bw::kPageFavourites) ov_.refresh();
    pg_.refresh();
}
void TuneConsole::reset_focused() {
    const bw::Row* r = ov_.current();
    if (!r) return;
    bool did = false;
    if (r->kind == bw::Row::Kind::kVar) did = reset_var(r->item.var, r->item.element);
    else if (r->kind == bw::Row::Kind::kCar) did = reset_car(r->car);
    else if (r->kind == bw::Row::Kind::kAction) { ask(Ask::kAll); return; }
    else return;
    note_ = did ? "Back to its default." : "Not changed: this is the game's own value.";
}
// tunenav2: RESET SECTION (the page's changed values) and RESET ALL; more than one value: the question first
void TuneConsole::ask(Ask what) {
    std::vector<bw::Change> cs = what == Ask::kAll ? ov_.changes() : ov_.section_changes();
    const char* name = what == Ask::kAll ? "reset all" : "reset section";
    if (cs.empty()) {
        note_ = what == Ask::kAll ? "Nothing changed: every value is the game's own." : "Nothing changed on this page.";
        return;
    }
    if (what == Ask::kSection && cs.size() == 1) {
        reset_changes(cs);
        note_ = "1 value back to its default.";
        std::printf("[b3tune] %s %s: 1 value back to its default\n", name, ov_.crumbs().c_str());
        std::fflush(stdout);
        return;
    }
    ask_ = what;
    ask_items_ = std::move(cs);
    std::printf("[b3tune] %s: asked (%zu values)\n", name, ask_items_.size());
    std::fflush(stdout);
}
void TuneConsole::answer(bool yes) {
    const char* name = ask_ == Ask::kAll ? "reset all" : "reset section";
    if (yes) {
        if (ask_ == Ask::kAll && hooks_ && hooks_->reset) hooks_->reset(-1, -1);
        else reset_changes(ask_items_);
        note_ = plural(ask_items_.size(), "value") + " back to the default.";
        std::printf("[b3tune] %s: %zu values back to their defaults\n", name, ask_items_.size());
    } else {
        note_ = "Nothing reset.";
        std::printf("[b3tune] %s: cancelled\n", name);
    }
    std::fflush(stdout);
    ask_ = Ask::kNone;
    ask_items_.clear();
    car_ns_ = 0;
}

// tunenav3: the mouse, over the box only (elsewhere it is the game's): the wheel moves the cursor, a click on a row
// selects it (on the selected row: opens / acts as Enter), a click on the tab strip changes tab (its left half back), a
// right click goes back
bool TuneConsole::mouse(const SDL_Event& e) {
    if (!open_ || !geo_.valid) return false;
    float mx = 0, my = 0;
    if (e.type == SDL_EVENT_MOUSE_WHEEL) { mx = e.wheel.mouse_x; my = e.wheel.mouse_y; }
    else { mx = e.button.x; my = e.button.y; }
    if (mx < geo_.x || mx >= geo_.x + geo_.w || my < geo_.y || my >= geo_.y + geo_.h) return false;
    car_ns_ = 0;
    sync();
    pad_hints_ = false;
    if (ask_ != Ask::kNone || (ov_.searching() && e.type != SDL_EVENT_MOUSE_WHEEL)) return true;   // (the question and the search take keys)
    if (e.type == SDL_EVENT_MOUSE_WHEEL) {
        note_.clear();
        if (e.wheel.y != 0) ov_.move(e.wheel.y > 0 ? -3 : +3);
        return true;
    }
    note_.clear();
    if (e.button.button == SDL_BUTTON_RIGHT) { ov_.back(); return true; }
    if (e.button.button != SDL_BUTTON_LEFT) return true;
    if (my < geo_.strip_y1) { ov_.next_tab(mx < geo_.x + geo_.w * 0.5f ? -1 : 1); return true; }
    if (geo_.list_n == 0 || my < geo_.list_y) return true;
    const std::size_t k = std::size_t((my - geo_.list_y) / geo_.pitch);
    if (k >= geo_.list_n) return true;
    const std::size_t at = geo_.list_first + k;
    if (at == ov_.cursor()) confirm();
    else ov_.set_cursor(at);
    return true;
}

bool TuneConsole::handle(const SDL_Event& e) {
    if (e.type == SDL_EVENT_MOUSE_WHEEL || e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
        std::lock_guard<std::mutex> l(mu_);
        return mouse(e);
    }
    if (e.type != SDL_EVENT_KEY_DOWN) return false;
    std::lock_guard<std::mutex> l(mu_);
    const SDL_Keycode k = e.key.key;
    // rebind2: the overlay's key is a port action (F9 by default); a script's synthetic key has no scancode
    const bool toggle = e.key.scancode == SDL_SCANCODE_UNKNOWN
                            ? k == SDLK_F9
                            : (NativePad::instance().port_mask(std::uint16_t(e.key.scancode), e.key.mod) >> int(b3native::input::PortAction::TuneOverlay) & 1) != 0;
    if (toggle && !e.key.repeat) {
        if (!enabled_ || !hooks_) return false;
        if (open_) close();
        else open_ = true;
        pad_seen_ = false;
        std::printf("[b3tune] the F9 overlay %s\n", open_ ? "opened" : "closed");
        std::fflush(stdout);
        return true;
    }
    if (!open_) return false;
    car_ns_ = 0;   // the car's values now (the CHANGED page's rows)
    sync();
    const bool shift = (e.key.mod & SDL_KMOD_SHIFT) != 0, ctrl = (e.key.mod & SDL_KMOD_CTRL) != 0, alt = (e.key.mod & SDL_KMOD_ALT) != 0;
    const int mult = shift ? 10 : (ctrl || alt) ? -10 : 1;
    const bool modifier = k == SDLK_LSHIFT || k == SDLK_RSHIFT || k == SDLK_LCTRL || k == SDLK_RCTRL || k == SDLK_LALT || k == SDLK_RALT ||
                          k == SDLK_LGUI || k == SDLK_RGUI;
    if (!modifier) {
        pad_hints_ = false;
        note_.clear();
    }
    if (ask_ != Ask::kNone) {   // tunenav2: the question: Enter (or Y) yes, any other key no
        if (modifier || e.key.repeat) return true;
        answer(k == SDLK_RETURN || k == SDLK_KP_ENTER || k == SDLK_Y);
        return true;
    }
    if (k == SDLK_HOME || (k == SDLK_R && (!ov_.searching() || ctrl))) {   // R / Home: the value; Shift: the section; Ctrl: all
        if (ctrl) ask(Ask::kAll);
        else if (shift) ask(Ask::kSection);
        else reset_focused();
        return true;
    }
    if (ov_.searching()) {   // typing goes to the search
        switch (k) {
        case SDLK_ESCAPE: ov_.search_close(); return true;
        case SDLK_BACKSPACE:
            if (ov_.query().empty()) ov_.search_close();
            else ov_.search_erase();
            return true;
        case SDLK_UP: ov_.move(-1); return true;
        case SDLK_DOWN: ov_.move(+1); return true;
        case SDLK_PAGEUP: ov_.move(-int(kOverlayRows)); return true;
        case SDLK_PAGEDOWN: ov_.move(+int(kOverlayRows)); return true;
        case SDLK_LEFT: change(-1, mult); return true;
        case SDLK_RIGHT: change(+1, mult); return true;
        case SDLK_RETURN:
        case SDLK_KP_ENTER: confirm(); return true;
        default: break;
        }
        if (!ctrl && !alt && ((k >= SDLK_A && k <= SDLK_Z) || (k >= SDLK_0 && k <= SDLK_9) || k == SDLK_SPACE || k == SDLK_MINUS ||
                              k == SDLK_PERIOD || k == SDLK_SLASH)) {
            if (ov_.query().size() < 40) ov_.search_type(char(k));
            return true;
        }
        return true;   // (every other key stays in the overlay)
    }
    switch (k) {
    case SDLK_ESCAPE: close(); return true;
    case SDLK_UP: ov_.move(-1); return true;
    case SDLK_DOWN: ov_.move(+1); return true;
    case SDLK_PAGEUP: ov_.move(-int(kOverlayRows)); return true;
    case SDLK_PAGEDOWN: ov_.move(+int(kOverlayRows)); return true;
    case SDLK_TAB: ov_.next_tab(shift ? -1 : 1); return true;
    case SDLK_Q: ov_.next_tab(-1); return true;
    case SDLK_E: ov_.next_tab(+1); return true;
    case SDLK_LEFT: change(-1, mult); return true;
    case SDLK_RIGHT: change(+1, mult); return true;
    case SDLK_RETURN:
    case SDLK_KP_ENTER: confirm(); return true;
    case SDLK_BACKSPACE: ov_.back(); return true;
    case SDLK_F: if (ctrl) ov_.search_open(); else favourite(); return true;
    case SDLK_SLASH: ov_.search_open(); return true;
    default: return false;
    }
}

// devnav1: as b3settings::Menu's hold (menu.cpp): the press at once, then after 0.35 s every 0.07 s; `fast` (up / down)
// every 0.035 s from 1.5 s held and ~60 a second from 3 s
bool TuneConsole::Hold::step(bool now, float dt, bool fast) {
    if (!now) {
        held = false;
        return false;
    }
    if (!held) {
        held = true;
        time = 0;
        next = 0.35f;
        return true;
    }
    time += dt;
    if (time < next) return false;
    next = std::max(next + (!fast || time < 1.5f ? 0.07f : time < 3.0f ? 0.035f : 0.016f), time);
    return true;
}

void TuneConsole::pad(const std::array<std::uint8_t, 32>& report) {
    std::lock_guard<std::mutex> l(mu_);
    const std::uint64_t now = SDL_GetTicksNS();
    const float dt = pad_ns_ ? std::clamp(float(double(now - pad_ns_) * 1e-9), 0.0f, 0.1f) : 0.0f;
    pad_ns_ = now;
    if (!open_) { pad_seen_ = false; return; }
    if (!pad_seen_) {   // buttons held as it opened: no press until released
        last_pad_ = report;
        pad_seen_ = true;
        for (Hold& h : hold_) h = Hold{true, 0, 1e9f};
        return;
    }
    auto edge = [&](unsigned b) { return pressed(report, b) && !pressed(last_pad_, b); };
    const bool cross = edge(kCross), circle = edge(kCircle), tri = edge(kTriangle), square = edge(kSquare), start = edge(kStart);
    const bool l1 = edge(kL1), r1 = edge(kR1), select = edge(kSelect);
    const bool up = hold_[0].step(pressed(report, kUp), dt, true), down = hold_[1].step(pressed(report, kDown), dt, true);
    const bool left = hold_[2].step(pressed(report, kLeft), dt, false), right = hold_[3].step(pressed(report, kRight), dt, false);
    const int mult = pressed(report, kR2) ? 10 : pressed(report, kL2) ? -10 : 1;
    last_pad_ = report;
    const bool any = cross || circle || tri || square || start || l1 || r1 || select || up || down || left || right;
    if (any) {   // tunenav2: the key lines follow the pad; a note lasts until the next press
        pad_hints_ = true;
        note_.clear();
    }
    if (ask_ != Ask::kNone && any) {   // the question: Cross yes, any other button no (Start also closes)
        answer(cross && !start);
        if (start) close();
        return;
    }
    if (start) { close(); return; }
    if (any) car_ns_ = 0;
    sync();
    if (circle) { go_back(); return; }
    if (l1) ov_.next_tab(-1);
    if (r1) ov_.next_tab(+1);
    if (up) ov_.move(-1);
    if (down) ov_.move(+1);
    if (left) change(-1, mult);
    if (right) change(+1, mult);
    if (cross) confirm();
    if (tri) favourite();
    if (square) {
        if (pressed(report, kR2)) ask(Ask::kSection);   // R2 + Square: the section
        else reset_focused();
    }
    if (select) ask(Ask::kAll);
}

// ---- the offscreen script and shot views ----
bool TuneConsole::scripted() const {
    std::lock_guard<std::mutex> l(mu_);
    return !script_.empty() || std::getenv("B3TUNE_OVERLAY_SCRIPT");
}
bool TuneConsole::script_step(std::uint32_t poll) {
    std::unique_lock<std::mutex> l(mu_);
    if (!script_read_) {
        script_read_ = true;
        const char* s = std::getenv("B3TUNE_OVERLAY_SCRIPT");
        for (std::string_view t = s ? s : ""; !t.empty();) {
            const std::size_t semi = t.find(';');
            const std::string item(b3tune::trim(t.substr(0, semi)));
            t = semi == std::string_view::npos ? std::string_view() : t.substr(semi + 1);
            const std::size_t colon = item.find(':');
            if (colon == std::string::npos) continue;
            script_.push_back({std::uint32_t(std::strtoul(item.substr(0, colon).c_str(), nullptr, 10)), item.substr(colon + 1)});
        }
        std::stable_sort(script_.begin(), script_.end(), [](const Act& a, const Act& b) { return a.poll < b.poll; });
        if (!script_.empty()) {
            enabled_ = true;
            std::printf("[b3tune] overlay script: %zu actions\n", script_.size());
            std::fflush(stdout);
        }
    }
    bool acted = false;
    while (script_i_ < script_.size() && script_[script_i_].poll <= poll) {
        const Act a = script_[script_i_++];
        acted = true;
        std::printf("[b3tune] overlay script: poll %u (at %u): %s\n", a.poll, poll, a.what.c_str());
        std::fflush(stdout);
        auto key = [&](SDL_Keycode kc, SDL_Keymod mod) {
            SDL_Event e{};
            e.type = SDL_EVENT_KEY_DOWN;
            e.key.key = kc;
            e.key.mod = mod;
            l.unlock();
            handle(e);
            l.lock();
        };
        if (a.what == "open") {
            if (!open_ && hooks_) key(SDLK_F9, SDL_KMOD_NONE);
        } else if (a.what == "close") {
            close();
        } else if (a.what.rfind("key:", 0) == 0) {
            std::string name = a.what.substr(4);
            SDL_Keymod mod = SDL_KMOD_NONE;
            for (const char* m : {"+shift", "+ctrl", "+alt"})
                if (const std::size_t at = name.find(m); at != std::string::npos) {
                    mod = SDL_Keymod(mod | (m[1] == 's' ? SDL_KMOD_LSHIFT : m[1] == 'c' ? SDL_KMOD_LCTRL : SDL_KMOD_LALT));
                    name.erase(at, std::strlen(m));
                }
            const SDL_Keycode kc = SDL_GetKeyFromName(name.c_str());
            if (kc != SDLK_UNKNOWN) key(kc, mod);
            else std::printf("[b3tune] overlay script: no key '%s'\n", name.c_str());
        } else if (a.what.rfind("type:", 0) == 0) {
            for (char c : a.what.substr(5)) key(SDL_Keycode(c == ' ' ? SDLK_SPACE : SDL_Keycode(static_cast<unsigned char>(std::tolower(c)))), SDL_KMOD_NONE);
        }
    }
    return acted;
}
bool TuneConsole::shot_state(const std::string& state) {
    std::lock_guard<std::mutex> l(mu_);
    if (!hooks_) return false;
    car_ns_ = 0;
    sync();   // (first: the overlay kept below has the tree now)
    if (!shot_) {   // the overlay as it is, back after the shot views (a script may go on after them)
        shot_keep_ = ov_;
        shot_ask_keep_ = ask_;
        shot_pad_keep_ = pad_hints_;
    }
    std::string st = state;
    bool pad = false, asked = false;
    for (const char* suf : {"+pad", "+ask"})
        for (std::size_t at; (at = st.find(suf)) != std::string::npos;) {
            (suf[1] == 'p' ? pad : asked) = true;
            st.erase(at, std::strlen(suf));
        }
    if (ov_.searching()) ov_.search_close();
    ask_ = Ask::kNone;
    if (st.rfind("tab:", 0) == 0) {
        std::string rest = st.substr(4);
        ov_.set_tab(std::size_t(std::strtoul(rest.c_str(), nullptr, 10)) % bw::kCats);
        for (std::size_t slash; (slash = rest.find('/')) != std::string::npos;) {   // each row opened in turn
            rest = rest.substr(slash + 1);
            ov_.set_cursor(std::size_t(std::strtoul(rest.c_str(), nullptr, 10)));
            ov_.enter();
        }
    } else if (st.rfind("search:", 0) == 0) {
        ov_.search_open();
        for (char c : st.substr(7)) ov_.search_type(c);
    } else {
        return false;
    }
    pad_hints_ = pad;
    if (asked) {   // the question as RESET ALL puts it (nothing is reset: the shot's end takes it back)
        ask_ = Ask::kAll;
        ask_items_ = ov_.changes();
    }
    shot_ = true;
    return true;
}
void TuneConsole::end_shots() {
    std::lock_guard<std::mutex> l(mu_);
    if (!shot_) return;
    shot_ = false;
    ov_ = shot_keep_;
    ask_ = shot_ask_keep_;
    if (ask_ == Ask::kNone) ask_items_.clear();
    pad_hints_ = shot_pad_keep_;
}

// ---- the overlay's lines and its box ----
std::vector<TuneConsole::Line> TuneConsole::lines(std::size_t rows) {
    std::lock_guard<std::mutex> l(mu_);
    sync();
    return lines_locked(rows);
}
std::vector<TuneConsole::Line> TuneConsole::lines_locked(std::size_t rows) {
    std::vector<Line> out;
    auto head = [&](std::string t, Line::Role role, std::string v = {}) {
        Line l;
        l.text = std::move(t);
        l.value = std::move(v);
        l.heading = true;
        l.role = role;
        out.push_back(l);
    };
    auto cut = [](std::string s, std::size_t n) { return s.size() > n ? s.substr(0, n - 3) + "..." : s; };
    const std::vector<bw::Row>& rs = ov_.rows();
    const std::size_t cur = ov_.cursor();
    const std::size_t first = rs.size() <= rows ? 0 : std::min(cur > rows / 2 ? cur - rows / 2 : 0, rs.size() - rows);
    const std::size_t last = std::min(rs.size(), first + rows);
    // the tab strip: one either side of the current one; its right end: where the cursor is in the page
    {
        const std::size_t t = ov_.tab();
        std::string strip;
        for (int d = -1; d <= 1; ++d) {
            const std::size_t k = (t + bw::kCats + std::size_t(d + int(bw::kCats))) % bw::kCats;
            std::string name = bw::cat_info(bw::Cat(k)).tab;
            if (const std::size_t n = ov_.tab_changed(k)) name += " (" + std::to_string(n) + ")";   // tunenav2
            strip += (strip.empty() ? "" : "  ") + (d == 0 ? "[" + name + "]" : name);
        }
        head("TUNING " + std::to_string(t + 1) + "/" + std::to_string(bw::kCats) + "  < " + strip + " >", Line::kStrip,
             rs.empty() ? std::string() : std::to_string(std::min(cur, rs.size() - 1) + 1) + "/" + std::to_string(rs.size()));
        out.back().scroll_total = rs.size();
        out.back().scroll_first = first;
        out.back().scroll_vis = last - first;
    }
    head(ov_.crumbs() + (ov_.searching() ? "_" : ""), Line::kCrumbs);
    std::string full;   // the focused row's whole name and value (the list may have cut it)
    for (std::size_t i = first; i < last; ++i) {
        const bw::Row& r = rs[i];
        Line l;
        l.role = Line::kRow;
        l.cursor = i == cur && r.kind != bw::Row::Kind::kText;
        if (r.kind == bw::Row::Kind::kText) {
            l.text = r.label;
            l.heading = true;
            out.push_back(l);
            continue;
        }
        std::string label = row_label(ov_, r);   // tunenav2's "* " changed, "+ " a favourite: the gutter's marks now
        l.edited = row_changed(ov_, r);
        while (label.rfind("* ", 0) == 0 || label.rfind("+ ", 0) == 0) {
            if (l.mark.find(label[0]) == std::string::npos) l.mark += label[0];
            label.erase(0, 2);
        }
        if (r.kind == bw::Row::Kind::kVar) {
            if (r.elsewhere && (ov_.page() == bw::kPageSearch || ov_.page() == bw::kPageFavourites || ov_.page() == bw::kPageRecent ||
                                ov_.page() == bw::kPageStarter || ov_.page() == bw::kPageChanged)) {
                const auto [node, row] = ov_.tree().where(r.item.var, r.item.element);
                (void)row;
                if (node >= 0) label += "  - " + ov_.tree().node(node).label;   // its page: the car model, the section
            }
        }
        l.value = row_value(r);
        static const char kDef[] = " (default ";   // "<value> (default <x>)": the value and the default in their columns
        if (const std::size_t at = l.value.find(kDef); at != std::string::npos && l.value.back() == ')') {
            l.def = l.value.substr(at + sizeof kDef - 1, l.value.size() - at - sizeof kDef);
            l.value.erase(at);
        }
        l.text = label;
        if (l.cursor) full = label + (l.value.empty() ? "" : "  =  " + l.value) + (l.def.empty() ? "" : "  (DEFAULT " + l.def + ")");
        out.push_back(l);
    }
    const bw::Row* c = ov_.current();
    if (ask_ != Ask::kNone) {   // tunenav2: the question in place of the help and the keys
        auto prompt = [&](std::string t) {
            head(std::move(t), Line::kPlain);
            out.back().prompt = true;
        };
        prompt(ask_ == Ask::kAll ? "RESET ALL: " + upper_text(plural(ask_items_.size(), "changed value")) + " BACK TO THEIR DEFAULTS?"
                                 : "RESET SECTION: " + upper_text(plural(ask_items_.size(), "value")) + " OF " + cut(ov_.crumbs(), 30) + "?");
        prompt(pad_hints_ ? "CROSS: YES, RESET THEM      ANY OTHER BUTTON: NO" : "ENTER: YES, RESET THEM      ANY OTHER KEY: NO");
        for (int k = 0; k < 5; ++k) head("", Line::kPlain);   // (as many lines as the detail, help and keys: the box keeps its size)
        return out;
    }
    // the focused row in full, its help (or the last reset's note), then the keys: a keyboard's, or the pad's once it is used
    head(full, Line::kDetail);
    head(!note_.empty() ? note_ : c ? row_help(*c) : std::string(), Line::kHelp);   // (drawn over two lines, cut at words)
    const bool pad = pad_hints_;
    std::string k1;
    if (ov_.searching()) k1 = pad ? "LEFT/RIGHT CHANGE   CROSS GO THERE" : "TYPE A NAME   LEFT/RIGHT CHANGE   ENTER GO THERE";
    else if (c && c->kind == bw::Row::Kind::kVar)
        k1 = std::string(pad ? "LEFT/RIGHT CHANGE   R2 X10   L2 /10" : "LEFT/RIGHT CHANGE   SHIFT X10   CTRL /10") +
             (c->elsewhere ? (pad ? "   CROSS GO THERE" : "   ENTER GO THERE") : "");
    else if (c && c->kind == bw::Row::Kind::kCar)
        k1 = c->elsewhere ? (pad ? "CROSS GO THERE" : "ENTER GO THERE") : (pad ? "LEFT/RIGHT CHANGE   CROSS DO IT" : "LEFT/RIGHT CHANGE   ENTER DO IT");
    else if (c && c->kind == bw::Row::Kind::kAction) k1 = pad ? "CROSS: RESET ALL (IT ASKS FIRST)" : "ENTER: RESET ALL (IT ASKS FIRST)";
    else k1 = pad ? "CROSS OPEN   CIRCLE BACK" : "RIGHT OPEN   LEFT BACK";
    head(k1, Line::kKeys);
    head(pad ? "SQUARE RESET   TRIANGLE FAVOURITE   R2+SQUARE SECTION   SELECT ALL"
             : ov_.searching() ? "HOME RESET   SHIFT+HOME SECTION   CTRL+R ALL"
                               : "R RESET   F FAVOURITE   SHIFT+R SECTION   CTRL+R ALL",
         Line::kKeys);
    head(pad ? "L1/R1 TABS   CIRCLE BACK   START CLOSE"
             : ov_.searching() ? "BACKSPACE ERASE   ESC END SEARCH   " + NativePad::instance().port_key_text(b3native::input::PortAction::TuneOverlay) + " CLOSE"
                               : "TAB/Q/E TABS   / SEARCH   BACKSPACE BACK   " + NativePad::instance().port_key_text(b3native::input::PortAction::TuneOverlay) + " CLOSE",
         Line::kKeys);
    return out;
}

// `text` word-wrapped to `width` px over at most `max_lines` lines; the last is cut with "..." when the text is still longer
static std::vector<std::string> wrap_lines(std::string text, float width, float cap, float st, std::size_t max_lines) {
    std::vector<std::string> out;
    while (!text.empty() && out.size() < max_lines) {
        if (out.size() + 1 == max_lines || glyph::text_width(text, cap, st) <= width) {
            out.push_back(tunelayout::ellipsize(text, width, cap, st));
            break;
        }
        std::size_t at = 0;
        for (std::size_t sp = 0; (sp = text.find(' ', sp)) != std::string::npos; ++sp)
            if (sp > 0 && glyph::text_width(text.substr(0, sp), cap, st) <= width) at = sp;
        std::string line = at ? text.substr(0, at) : text;
        while (glyph::text_width(line, cap, st) > width && line.size() > 1) line.pop_back();   // (no space fits: cut hard)
        text.erase(0, line.size());
        while (!text.empty() && text.front() == ' ') text.erase(text.begin());
        out.push_back(line);
    }
    return out;
}

TuneConsole::Frame TuneConsole::frame_locked(int w, int h) {
    Frame fr;
    constexpr std::size_t kBlock = 4;   // the focused row's name and its help share four lines (the box keeps its size)
    // the list's length from the window's height (the layout may then still shrink the type), at the largest type
    const tunelayout::Metrics hi = tunelayout::metrics(std::max(0.85f, float(h) / 720.0f));
    const int by_height = int((tunelayout::max_panel_h(h) - 2 * hi.pad) / hi.pitch()) - 10;
    fr.lines = lines_locked(std::size_t(std::clamp(by_height, 8, int(kOverlayRows))));
    std::vector<tunelayout::Item> in;
    std::size_t block = 0, detail = fr.lines.size(), help = fr.lines.size();
    for (std::size_t i = 0; i < fr.lines.size(); ++i) {
        const Line& l = fr.lines[i];
        if (l.role == Line::kHelp) {
            help = i;
            continue;
        }
        tunelayout::Item it;
        it.text = l.text;
        it.value = l.value;
        it.def = l.def;
        it.mark = l.mark;
        it.wide = l.heading;
        if (l.role == Line::kDetail) {   // the block: filled below once the box's width is known (its words must not widen it)
            detail = i;
            block = in.size();
            it.text.clear();
            for (std::size_t k = 0; k < kBlock; ++k) {
                in.push_back(it);
                fr.tag.push_back(int(i));
                fr.role.push_back(Line::kDetail);
            }
            continue;
        }
        in.push_back(it);
        fr.tag.push_back(int(i));
        fr.role.push_back(l.role);
    }
    fr.fit = tunelayout::fit(in, w, h);
    if (detail < fr.lines.size()) {
        const float room = fr.fit.panel_w - 2 * fr.fit.m.pad;
        const std::vector<std::string> d = wrap_lines(fr.lines[detail].text, room, fr.fit.m.cap, fr.fit.m.st, 2);
        const std::vector<std::string> hp =
            help < fr.lines.size() ? wrap_lines(fr.lines[help].text, room, fr.fit.m.cap, fr.fit.m.st, kBlock - std::max<std::size_t>(d.size(), 1))
                                   : std::vector<std::string>();
        for (std::size_t k = 0; k < kBlock; ++k) {
            const bool is_detail = k < std::max<std::size_t>(d.size(), 1);
            fr.fit.items[block + k].text = is_detail ? (k < d.size() ? d[k] : std::string()) : (k - std::max<std::size_t>(d.size(), 1) < hp.size() ? hp[k - std::max<std::size_t>(d.size(), 1)] : std::string());
            fr.role[block + k] = is_detail ? Line::kDetail : Line::kHelp;
        }
    }
    return fr;
}

TuneConsole::Frame TuneConsole::frame(int w, int h) {
    std::lock_guard<std::mutex> l(mu_);
    sync();
    return frame_locked(w, h);
}

void TuneConsole::draw_list(b3gfx::Device& dev, int w, int h, std::vector<b3gfx::DrawItem>& out) {
    if (epoch_.changed(dev)) tex_ = vb_ = ib_ = b3gfx::kNone;   // a track load released every id (the game's own took them): make them again
    Frame fr;
    {
        std::lock_guard<std::mutex> l(mu_);
        if ((!open_ && !shot_) || !hooks_ || w <= 0 || h <= 0) return;
        sync();
        save_usage(false);
        fr = frame_locked(w, h);
    }
    const tunelayout::Fit& ft = fr.fit;
    const tunelayout::Metrics& m = ft.m;
    auto line_of = [&](std::size_t i) -> const Line& { return fr.lines[std::size_t(fr.tag[i])]; };
    std::string text = std::to_string(w) + "x" + std::to_string(h) + "\n";
    for (std::size_t i = 0; i < ft.items.size(); ++i) {
        const Line& l = line_of(i);
        text += (l.cursor ? "1" : "0") + std::string(l.edited ? "1" : "0") + (l.heading ? "1" : "0") + (l.prompt ? "1" : "0") +
                std::to_string(int(fr.role[i])) + ft.items[i].mark + "\t" + ft.items[i].text + "\t" + ft.items[i].value + "\t" + ft.items[i].def + "\n";
    }
    text += std::to_string(fr.lines[0].scroll_first) + "/" + std::to_string(fr.lines[0].scroll_total);
    tex_w_ = int(ft.panel_w);
    tex_h_ = int(ft.panel_h);
    {   // (the log of a bug report: how the box was fitted to the window, once per size)
        static int lw = 0, lh = 0, lcut = -1;
        if (w != lw || h != lh || ft.cut != lcut) {
            lw = w, lh = h, lcut = ft.cut;
            std::printf("[b3tune] box %dx%d in a %dx%d window: text %.2fx%s, %d label%s cut\n", tex_w_, tex_h_, w, h, double(m.s),
                        ft.floor_scale ? " (the smallest)" : "", ft.cut, ft.cut == 1 ? "" : "s");
            std::fflush(stdout);
        }
    }
    if (text != built_text_ || built_h_ != h || tex_ == b3gfx::kNone) {
        const float s = m.s, pad = m.pad, gap = m.gap, cap = m.cap, st = m.st;
        glyph::Canvas c(tex_w_, tex_h_);
        const float bg[4] = {0.025f, 0.03f, 0.045f, 0.94f}, edge[4] = {0.30f, 0.62f, 0.95f, 0.95f};
        const float stripbg[4] = {0.10f, 0.22f, 0.40f, 1.0f}, white[4] = {0.96f, 0.98f, 1.0f, 1.0f};
        const float headc[4] = {0.55f, 0.80f, 1.0f, 1.0f}, bodyc[4] = {0.95f, 0.95f, 0.95f, 1.0f}, dimc[4] = {0.66f, 0.70f, 0.76f, 1.0f},
                    cur[4] = {1.0f, 0.86f, 0.30f, 1.0f}, ed[4] = {0.50f, 1.0f, 0.55f, 1.0f}, bar[4] = {1.0f, 0.82f, 0.25f, 0.24f},
                    rule[4] = {0.30f, 0.62f, 0.95f, 0.50f}, track[4] = {1.0f, 1.0f, 1.0f, 0.14f}, thumb[4] = {1.0f, 0.86f, 0.30f, 0.9f};
        const float W = float(tex_w_), H = float(tex_h_);
        c.box(W * 0.5f, H * 0.5f, W * 0.5f, H * 0.5f, 6 * s, bg);
        c.box_outline(W * 0.5f, H * 0.5f, W * 0.5f - s, H * 0.5f - s, 5 * s, std::max(1.0f, 1.2f * s), edge);
        const float def_right = W - pad, val_right = def_right - (ft.def_w > 0 ? m.col + ft.def_w : 0.0f);
        float y = pad, row0 = -1, row1 = 0;
        auto put = [&](const std::string& t, float x, const float (&col)[4]) {
            std::vector<glyph::Line> pl;
            glyph::text(t, x, y, cap, st, pl);
            c.strokes(pl, st, col);
        };
        auto put_right = [&](const std::string& t, float right, const float (&col)[4]) { put(t, right - glyph::text_width(t, cap, st), col); };
        const float hair = 0.5f * std::max(1.0f, s);
        for (std::size_t i = 0; i < ft.items.size(); ++i) {
            const tunelayout::Item& it = ft.items[i];
            const Line& l = line_of(i);
            const Line::Role role = fr.role[i];
            if (l.role == Line::kRow) {
                if (row0 < 0) row0 = y;
                row1 = y + cap;
            }
            if (l.role == Line::kStrip) c.box(W * 0.5f, y + cap * 0.5f, W * 0.5f - 3 * s, cap * 0.5f + gap * 0.7f, 3 * s, stripbg);
            if (l.cursor) {
                c.box(W * 0.5f, y + cap * 0.5f, W * 0.5f - 3 * s, cap * 0.5f + gap * 0.38f, 2 * s, bar);
                c.box(pad * 0.5f, y + cap * 0.5f, 1.5f * s, cap * 0.5f + gap * 0.3f, 1 * s, cur);
            }
            const float(&col)[4] = (l.cursor || l.prompt)                                ? cur
                                   : l.role == Line::kStrip                               ? white
                                   : role == Line::kCrumbs || role == Line::kDetail         ? headc
                                   : role == Line::kHelp || role == Line::kKeys             ? dimc
                                   : l.heading                                            ? headc
                                   : l.edited                                             ? ed
                                                                                          : bodyc;
            if (l.role == Line::kRow && !l.heading) {
                if (!it.mark.empty()) put(it.mark, pad, l.cursor ? cur : ed);
                put(it.text, pad + m.gutter, col);
                if (!it.value.empty()) put_right(it.value, val_right, col);
                if (!it.def.empty()) put_right(it.def, def_right, l.cursor ? cur : dimc);
            } else {
                if (!it.text.empty()) put(it.text, pad, col);
                if (!it.value.empty()) put_right(it.value, W - pad, col);
            }
            if (l.role == Line::kCrumbs) {
                c.box(W * 0.5f, y + cap + gap * 0.5f, W * 0.5f - pad * 0.5f, hair, 0, rule);
                // the columns' heads, when the crumbs leave room
                const float room_x = pad + glyph::text_width(it.text, cap, st) + 2 * m.col;
                const float dw = glyph::text_width("DEFAULT", cap, st), vw = glyph::text_width("VALUE", cap, st);
                if (ft.def_w > 0 && def_right - dw > room_x) {
                    put_right("DEFAULT", def_right, dimc);
                    if (val_right - vw - dw - m.col > room_x) put_right("VALUE", val_right, dimc);
                }
            }
            if (role == Line::kDetail && (i == 0 || fr.role[i - 1] != Line::kDetail)) c.box(W * 0.5f, y - gap * 0.5f, W * 0.5f - pad * 0.5f, hair, 0, rule);
            y += cap + gap;
        }
        const Line& strip = fr.lines[0];
        if (row0 >= 0 && strip.scroll_total > strip.scroll_vis) {   // the scroll bar, in the list's right margin
            const float tx = W - pad * 0.45f, th = row1 - row0, n = float(strip.scroll_total);
            c.box(tx, row0 + th * 0.5f, 1.2f * s, th * 0.5f, 1 * s, track);
            const float t0 = th * float(strip.scroll_first) / n, t1 = th * float(strip.scroll_first + strip.scroll_vis) / n;
            c.box(tx, row0 + (t0 + t1) * 0.5f, 1.6f * s, std::max(2.0f * s, (t1 - t0) * 0.5f), 1 * s, thumb);
        }
        std::vector<std::uint8_t> px(std::size_t(tex_w_) * std::size_t(tex_h_) * 4, 0);
        c.store(px.data(), tex_w_, 0, 0);
        dev.update_texture(tex_, tex_w_, tex_h_, px.data());
        built_text_ = text;
        built_h_ = h;
    }
    // at the left, below the HUD's top row; the 2D stage's half-intensity GS frame as the notice (RGB x 64, alpha x 128)
    const float x0 = std::round(m.margin * float(w)), y0 = std::round(0.10f * float(h));
    const float x1 = x0 + float(tex_w_), y1 = y0 + float(tex_h_);
    {   // for the mouse: where the box and its list are (window pixels)
        std::lock_guard<std::mutex> l(mu_);
        geo_ = Geo{};
        geo_.x = x0;
        geo_.y = y0;
        geo_.w = float(tex_w_);
        geo_.h = float(tex_h_);
        geo_.pitch = m.pitch();
        for (std::size_t i = 0; i < fr.lines.size(); ++i)
            if (fr.lines[i].role == Line::kRow) {
                geo_.list_y = y0 + m.pad + float(i) * m.pitch();
                geo_.list_first = fr.lines[0].scroll_first;
                geo_.list_n = fr.lines[0].scroll_vis;
                break;
            }
        geo_.strip_y1 = y0 + m.pad + m.pitch();
        geo_.valid = true;
    }
    const std::uint32_t rgba = 64u | 64u << 8 | 64u << 16 | 128u << 24;
    const float xs[4] = {x0, x1, x1, x0}, ys[4] = {y0, y0, y1, y1}, us[4] = {0, 1, 1, 0}, vv[4] = {0, 0, 1, 1};
    b3gfx::Vertex vs[4];
    for (int k = 0; k < 4; ++k) {
        vs[k] = b3gfx::Vertex{};
        vs[k].pos[0] = xs[k];
        vs[k].pos[1] = ys[k];
        vs[k].uv[0] = us[k];
        vs[k].uv[1] = vv[k];
        vs[k].rgba = rgba;
    }
    const std::uint32_t is[6] = {0, 1, 2, 0, 2, 3};
    dev.update_vertex_buffer(vb_, vs, 4);
    dev.update_index_buffer(ib_, is, 6);
    b3gfx::DrawItem it;
    it.overlay = true;
    it.shading = b3gfx::Shading::kTrack;
    it.track_type = 0;
    it.textured = true;
    it.texture = tex_;
    it.gs_alpha = 0x44;
    it.overlay_blend = true;
    it.point_sample = true;
    const float mm[16] = {2.0f / float(w), 0, 0, 0, 0, -2.0f / float(h), 0, 0, 0, 0, 1, 0, -1, 1, 0.5f, 1};
    std::memcpy(it.model, mm, sizeof mm);
    it.vertices = vb_;
    it.indices = ib_;
    it.first_index = 0;
    it.index_count = 6;
    out.push_back(it);
}

}  // namespace b3replay
