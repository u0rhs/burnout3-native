// replay_stream.h -- media1: a replay being recorded is on disk while the session runs (src/native/replay_file.h).
// At every RAM checkpoint (kCheckEvery VBlanks, 10 s of game time) native_play hands this writer what the recording
// gained since the last checkpoint (checkpoint(): the new pad / text / setting / check entries and the small header
// fields -- the lists are append-only, so that is O(the last 10 s), whatever the session's length). The writer's own
// thread keeps a copy of the recording, appends each part, encodes it as a complete replay file (the same format:
// header, sections, CRC; HEAD's last VBlank = the checkpoint's) and writes it beside the file, renamed over it
// (replay_file.h write_bytes). A kill, a power loss or a hard crash then leaves the replay up to the last checkpoint,
// which plays like any other (it ends there); the session's normal end writes the whole file as before. Parts that
// arrive while a write runs are all applied before the next encode (only the newest checkpoint is encoded).
#ifndef B3NATIVE_REPLAY_STREAM_H
#define B3NATIVE_REPLAY_STREAM_H

#include "replay_file.h"

#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <vector>
#include <string>
#include <thread>
#include <utility>

namespace b3native::replay {

// The checkpoint's file: the recording so far with HEAD's last VBlank `v` and notes saying the session went on.
// `r`'s vblanks and info are left as they were.
inline Bytes encode_partial(Replay& r, std::uint64_t v) {
    const std::uint64_t vblanks = r.vblanks;
    const std::size_t info = r.info.size();
    r.vblanks = v;
    r.info.emplace_back("end", "not ended here: saved at VBlank " + std::to_string(v) + " while the session ran on");
    r.info.emplace_back("length", format_length(v));
    Bytes b = encode(r);
    r.info.resize(info);
    r.vblanks = vblanks;
    return b;
}

// What a checkpoint adds to the writer's copy: the header fields (the card only the first time: it is the card at the
// boot and never changes) and the list entries since the last checkpoint
struct StreamPart {
    std::uint64_t vblank = 0;
    Replay head;          // its lists empty
    bool card = false;    // head's CARD fields to take
    bool whole = false;   // the lists below are whole (the copy's are replaced)
    std::vector<PadOp> pads;
    std::vector<TextOp> text;
    std::vector<SettingOp> settings;
    std::vector<Check> checks;
};

class StreamWriter {
public:
    // `rotate_dir` (automatic recordings): rotate() there after the first write, so a session that never ends cleanly
    // still keeps the folder at the newest kKeepRecorded
    explicit StreamWriter(fs::path file, fs::path rotate_dir = {}) : file_(std::move(file)), rotate_dir_(std::move(rotate_dir)) {
        thread_ = std::thread([this] { run(); });
    }
    ~StreamWriter() { stop(); }
    StreamWriter(const StreamWriter&) = delete;
    StreamWriter& operator=(const StreamWriter&) = delete;

    // Game thread, at a checkpoint (VBlank v, every op of v taken): the entries `r` gained since the last call. `r`'s
    // lists only ever grow (Recorder); one that shrank is sent whole again.
    void checkpoint(const Replay& r, std::uint64_t v) {
        StreamPart p;
        p.vblank = v;
        Replay& h = p.head;
        h.format = r.format; h.vblanks = r.vblanks; h.started = r.started; h.utc_offset = r.utc_offset; h.version = r.version;
        h.program_size = r.program_size; h.program_fnv = r.program_fnv; h.info = r.info; h.options = r.options;
        h.neutral = r.neutral; h.check_every = r.check_every;
        if (!card_sent_) {
            h.card = r.card; h.card_slot = r.card_slot; h.card_sav = r.card_sav;
            p.card = card_sent_ = true;
        }
        auto tail = [](const auto& all, std::size_t& sent, auto& out, bool& whole) {
            if (all.size() < sent) { sent = 0; whole = true; }
            out.assign(all.begin() + std::ptrdiff_t(sent), all.end());
            sent = all.size();
        };
        bool whole = false;
        tail(r.pads, sent_pads_, p.pads, whole);
        tail(r.text, sent_text_, p.text, whole);
        tail(r.settings, sent_settings_, p.settings, whole);
        tail(r.checks, sent_checks_, p.checks, whole);
        if (whole) {   // (never with a Recorder) the copy rebuilt from the whole lists
            sent_pads_ = sent_text_ = sent_settings_ = sent_checks_ = 0;
            p.pads = r.pads; p.text = r.text; p.settings = r.settings; p.checks = r.checks;
            sent_pads_ = r.pads.size(); sent_text_ = r.text.size(); sent_settings_ = r.settings.size(); sent_checks_ = r.checks.size();
        }
        p.whole = whole;
        {
            std::lock_guard<std::mutex> lock(m_);
            parts_.push_back(std::move(p));
        }
        cv_.notify_one();
    }
    // Writes what is waiting, then ends the thread (before the session's own final write_file of the same path)
    void stop() {
        {
            std::lock_guard<std::mutex> lock(m_);
            if (stop_) return;
            stop_ = true;
        }
        cv_.notify_one();
        if (thread_.joinable()) thread_.join();
    }
    std::uint64_t written() const { std::lock_guard<std::mutex> lock(m_); return written_; }
    std::string error() const { std::lock_guard<std::mutex> lock(m_); return error_; }

private:
    void apply(StreamPart& p) {
        Replay& c = copy_;
        Replay& h = p.head;
        c.format = h.format; c.started = h.started; c.utc_offset = h.utc_offset; c.version = std::move(h.version);
        c.program_size = h.program_size; c.program_fnv = h.program_fnv; c.info = std::move(h.info); c.options = std::move(h.options);
        c.neutral = h.neutral; c.check_every = h.check_every;
        if (p.card) { c.card = h.card; c.card_slot = h.card_slot; c.card_sav = std::move(h.card_sav); }
        if (p.whole) { c.pads.clear(); c.text.clear(); c.settings.clear(); c.checks.clear(); }
        c.pads.insert(c.pads.end(), p.pads.begin(), p.pads.end());
        c.text.insert(c.text.end(), p.text.begin(), p.text.end());
        c.settings.insert(c.settings.end(), p.settings.begin(), p.settings.end());
        c.checks.insert(c.checks.end(), p.checks.begin(), p.checks.end());
    }
    void run() {
        for (;;) {
            std::vector<StreamPart> parts;
            {
                std::unique_lock<std::mutex> lock(m_);
                cv_.wait(lock, [this] { return !parts_.empty() || stop_; });
                if (parts_.empty()) return;   // stopping, nothing waiting
                parts.swap(parts_);
            }
            for (StreamPart& p : parts) apply(p);
            const Bytes b = encode_partial(copy_, parts.back().vblank);
            std::string why;
            const bool ok = write_bytes(file_, b, &why);
            bool first = false;
            {
                std::lock_guard<std::mutex> lock(m_);
                if (ok) first = ++written_ == 1;
                else if (error_.empty()) error_ = why;
            }
            if (first && !rotate_dir_.empty()) rotate(rotate_dir_);
        }
    }
    fs::path file_, rotate_dir_;
    mutable std::mutex m_;
    std::condition_variable cv_;
    std::vector<StreamPart> parts_;   // handed over, not yet applied
    Replay copy_;                     // the writer thread's copy of the recording
    std::size_t sent_pads_ = 0, sent_text_ = 0, sent_settings_ = 0, sent_checks_ = 0;   // (game thread)
    bool card_sent_ = false;
    bool stop_ = false;
    std::uint64_t written_ = 0;
    std::string error_;
    std::thread thread_;
};

}  // namespace b3native::replay

#endif
