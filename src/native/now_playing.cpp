// now_playing.cpp -- the system-independent parts of src/native/now_playing.h: the text forms, the file reader, the
// poller and the popup's decisions. The system readers: now_playing_win.cpp (Windows), now_playing_mpris.cpp (Linux).
#include "now_playing.h"
#include "b3port/fileio.h"   // fsrobust: UTF-8 paths

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace b3nowplaying {

namespace {
constexpr char kSep = '\x1F';
}  // namespace

std::string describe(const Track& t) {
    if (t.status == Status::kNone) return "nothing playing";
    std::string s;
    if (!t.app.empty()) s += t.app + ": ";
    if (!t.artist.empty()) s += t.artist + " - ";
    s += t.title.empty() ? std::string("(no title)") : t.title;
    if (!t.album.empty()) s += " (" + t.album + ")";
    s += t.status == Status::kPlaying ? ", playing" : ", paused";
    return s;
}

std::string encode(const Track& t) {
    std::string s(1, char('0' + int(t.status)));
    for (const std::string* f : {&t.title, &t.artist, &t.album, &t.app}) {
        s += kSep;
        s += *f;
    }
    return s;
}

bool decode(const std::string& s, Track& t) {
    if (s.empty() || s[0] < '0' || s[0] > '2') return false;
    std::vector<std::string> f;
    std::size_t at = 1;
    while (at < s.size() && s[at] == kSep) {
        const std::size_t next = s.find(kSep, at + 1);
        f.push_back(s.substr(at + 1, next == std::string::npos ? std::string::npos : next - at - 1));
        at = next == std::string::npos ? s.size() : next;
    }
    if (at != s.size() || f.size() != 4) return false;
    t.status = Status(s[0] - '0');
    t.title = f[0];
    t.artist = f[1];
    t.album = f[2];
    t.app = f[3];
    return true;
}

// ---- the file reader ----
namespace {
class FileProvider : public Provider {
public:
    explicit FileProvider(std::string path) : path_(std::move(path)) {}
    const char* name() const override { return "file"; }
    bool read(Track& out, std::string* why) override {
        std::ifstream f(b3io::u8path(path_), std::ios::binary);
        if (!f) {
            if (why) *why = "cannot read " + path_;
            return false;
        }
        Track t;
        std::string line;
        bool fail = false;
        while (std::getline(f, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            const std::size_t eq = line.find('=');
            if (line.empty() || line[0] == '#' || eq == std::string::npos) continue;
            const std::string k = line.substr(0, eq), v = line.substr(eq + 1);
            if (k == "status") t.status = v == "playing" ? Status::kPlaying : v == "paused" ? Status::kPaused : Status::kNone;
            else if (k == "title") t.title = v;
            else if (k == "artist") t.artist = v;
            else if (k == "album") t.album = v;
            else if (k == "app") t.app = v;
            else if (k == "fail") fail = v == "1";   // tests: a reader that fails
        }
        if (fail) {
            if (why) *why = "the file says the read fails";
            return false;
        }
        out = t;
        return true;
    }

private:
    std::string path_;
};
}  // namespace

std::unique_ptr<Provider> file_provider(const std::string& path) { return std::make_unique<FileProvider>(path); }

std::unique_ptr<Provider> system_provider(std::string* why) {
#if defined(_WIN32)
    return smtc_provider(why);
#elif defined(__linux__)
    return mpris_provider(why);
#else
    if (why) *why = "macOS has no public interface for another app's now-playing song (MediaRemote is private and "
                    "refused to other apps since macOS 15.4)";
    return nullptr;
#endif
}

const char* system_interface() {
#if defined(_WIN32)
    return "Windows media sessions";
#elif defined(__linux__)
    return "MPRIS (D-Bus)";
#else
    return nullptr;
#endif
}

std::string app_name_from_id(const std::string& id) {
    std::string s = id;
    if (const std::size_t bang = s.rfind('!'); bang != std::string::npos) s = s.substr(bang + 1);   // a packaged app's entry point
    if (const std::size_t slash = s.find_last_of("/\\"); slash != std::string::npos) s = s.substr(slash + 1);
    if (s.size() > 4) {
        std::string ext = s.substr(s.size() - 4);
        for (char& c : ext) c = char(std::tolower(static_cast<unsigned char>(c)));
        if (ext == ".exe") s.resize(s.size() - 4);
    }
    if (s.rfind("Microsoft.", 0) == 0) s = s.substr(10);   // "Microsoft.ZuneMusic" -> "ZuneMusic"
    const bool hash = s.size() >= 8 && std::all_of(s.begin(), s.end(), [](char c) { return std::isxdigit(static_cast<unsigned char>(c)) != 0; });
    if (hash) return "";
    if (!s.empty() && s[0] >= 'a' && s[0] <= 'z') s[0] = char(s[0] - 'a' + 'A');
    return s;
}

int choose_player(const std::vector<Player>& players) {
    for (std::size_t k = 0; k < players.size(); ++k)
        if (players[k].track.status == Status::kPlaying) return int(k);
    for (std::size_t k = 0; k < players.size(); ++k)
        if (players[k].track.status == Status::kPaused) return int(k);
    return -1;
}

// ---- the poller ----
Poller::Poller(Factory factory, std::chrono::milliseconds every, bool threaded) : factory_(std::move(factory)), every_(every) {
    snap_.why = "off";
    if (threaded) thread_ = std::thread([this] { run(); });
}

Poller::~Poller() {
    {
        std::lock_guard<std::mutex> lock(m_);
        stop_ = true;
    }
    cv_.notify_all();
    if (thread_.joinable()) thread_.join();
    else provider_.reset();
}

void Poller::set_enabled(bool on) {
    {
        std::lock_guard<std::mutex> lock(m_);
        if (enabled_ == on) return;
        enabled_ = on;
        if (!on) {
            snap_.track = Track{};
            snap_.ok = false;
            snap_.why = "off";
        }
        retry_at_ = {};
    }
    cv_.notify_all();
}

bool Poller::enabled() const {
    std::lock_guard<std::mutex> lock(m_);
    return enabled_;
}

Poller::Snapshot Poller::snapshot() const {
    std::lock_guard<std::mutex> lock(m_);
    return snap_;
}

std::uint64_t Poller::providers_created() const {
    std::lock_guard<std::mutex> lock(m_);
    return created_;
}

void Poller::poll_once() {
    std::unique_lock<std::mutex> lock(m_);
    step(lock);
}

// One read (or the provider released while off). Called with m_ held; the provider's calls run without it.
void Poller::step(std::unique_lock<std::mutex>& lock) {
    if (!enabled_) {
        if (provider_) {
            std::unique_ptr<Provider> p = std::move(provider_);
            lock.unlock();
            p.reset();   // on this thread (WinRT: the thread that initialised it)
            lock.lock();
        }
        return;
    }
    if (!provider_) {
        if (std::chrono::steady_clock::now() < retry_at_) return;
        lock.unlock();
        std::string why;
        std::unique_ptr<Provider> p = factory_ ? factory_(&why) : nullptr;
        lock.lock();
        ++created_;
        if (!p) {
            snap_.track = Track{};
            snap_.ok = false;
            snap_.why = why.empty() ? "no reader" : why;
            snap_.provider.clear();
            retry_at_ = std::chrono::steady_clock::now() + std::chrono::seconds(10);   // a player or a bus may come later
            return;
        }
        provider_ = std::move(p);
        snap_.provider = provider_->name();
        if (!enabled_) return;   // switched off meanwhile: released at the next step
    }
    Provider* p = provider_.get();
    lock.unlock();
    Track t;
    std::string why;
    const bool ok = p->read(t, &why);
    lock.lock();
    if (!enabled_) return;
    ++snap_.reads;
    snap_.ok = ok;
    if (ok) {
        snap_.track = t;
        snap_.why.clear();
        failed_in_row_ = 0;
    } else {   // a failed read keeps the last track a few times (a slow player: no second popup of the same song)
        ++snap_.failures;
        if (++failed_in_row_ >= kKeepOnFailure) snap_.track = Track{};
        snap_.why = why.empty() ? "the read failed" : why;
    }
}

void Poller::run() {
    std::unique_lock<std::mutex> lock(m_);
    while (!stop_) {
        step(lock);
        if (stop_) break;
        cv_.wait_for(lock, every_);
    }
    if (provider_) {
        std::unique_ptr<Provider> p = std::move(provider_);
        lock.unlock();
        p.reset();
    }
}

// ---- the popup's decisions ----
std::optional<PopupLines> PopupDecider::frame(const Track& t, bool display_free) {
    if (t.status == Status::kNone) {
        shown_.clear();
        pending_ = false;
        return std::nullopt;
    }
    if (t.status != Status::kPlaying || t.title.empty()) {   // paused: the same track resumed shows nothing
        pending_ = false;
        return std::nullopt;
    }
    std::string key = t.title;
    key += kSep;
    key += t.artist;
    key += kSep;
    key += t.album;
    if (key == shown_) {
        pending_ = false;
        return std::nullopt;
    }
    pending_ = true;
    if (!display_free) return std::nullopt;
    shown_ = std::move(key);
    pending_ = false;
    return PopupLines{t.title, t.artist, t.album.empty() ? t.app : t.album};
}

}  // namespace b3nowplaying
