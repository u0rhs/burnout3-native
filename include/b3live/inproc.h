// The in-process live feed (inproc1, docs/native-renderer.md §23): the scene hook's per-poll records handed to the
// native renderer inside the game process, with no ring and no second process.
//
// Producer: the game's thread (src/gameview/scene_capture.cpp's inproc_publish through b3render.dll's C ABI,
// src/b3replay/inproc.cpp). push() is one malloc, one memcpy and a compare-exchange onto a list head that the consumer
// only ever swaps out whole: the producer never blocks and never waits for the consumer.
// Consumer: the renderer's thread (LiveFeed, src/b3replay/live.cpp). take() swaps the whole list out and returns it in
// push order.
//
// Unlike the shared-memory ring (ring.h) nothing is overwritten, so a slow reader never loses records and needs no
// keyframe except the first. A byte cap (records pushed but not yet released by the consumer) is a safety net only:
// over it a record is dropped, and the producer's next record must be a keyframe (want_key()).
#ifndef B3LIVE_INPROC_H
#define B3LIVE_INPROC_H

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "b3live/ring.h"   // kDelta / kKeyframe, now_ns

namespace b3live {

struct InprocRecord {
    InprocRecord* next;
    std::uint64_t ns;      // producer's steady clock at push
    std::uint64_t sched;   // lat1: the host time the input of the tick it shows was latched (b3r_submit_at; 0: none)
    std::uint32_t kind;    // kDelta / kKeyframe
    std::uint32_t size;    // body bytes
    std::uint8_t* body() { return reinterpret_cast<std::uint8_t*>(this + 1); }
};

class Inproc {
public:
    explicit Inproc(std::uint64_t cap_bytes = 512ull << 20) : cap_(cap_bytes) {}
    Inproc(const Inproc&) = delete;
    Inproc& operator=(const Inproc&) = delete;
    ~Inproc() { release(take()); }

    // ---- producer (one thread) ----
    // True when the next record must be a keyframe: none sent yet, a record was dropped, or the consumer asked.
    bool want_key() {
        if (!sent_key_ || drop_pending_) return true;
        return key_request_.exchange(0, std::memory_order_acq_rel) != 0;
    }
    // Queues a copy of the record. False: dropped (over the cap, or no memory); the next record must then be a keyframe.
    bool push(std::uint32_t kind, const void* body, std::size_t n, std::uint64_t sched = 0) {
        polls.fetch_add(1, std::memory_order_relaxed);
        if (kind != kKeyframe && (!sent_key_ || drop_pending_)) { drop(); return false; }   // a delta after a gap
        if (queued_.load(std::memory_order_relaxed) + n > cap_ || n > 0xFFFFFFFFull) { drop(); return false; }
        auto* r = static_cast<InprocRecord*>(std::malloc(sizeof(InprocRecord) + n));
        if (r == nullptr) { drop(); return false; }
        r->ns = now_ns();
        r->sched = sched;
        r->kind = kind;
        r->size = std::uint32_t(n);
        if (n) std::memcpy(r->body(), body, n);
        queued_.fetch_add(n, std::memory_order_relaxed);
        r->next = head_.load(std::memory_order_relaxed);
        while (!head_.compare_exchange_weak(r->next, r, std::memory_order_release, std::memory_order_relaxed)) {}
        records.fetch_add(1, std::memory_order_relaxed);
        if (kind == kKeyframe) { keyframes.fetch_add(1, std::memory_order_relaxed); sent_key_ = true; drop_pending_ = false; }
        return true;
    }
    // After the last record (session end).
    void close() { closed_.store(1, std::memory_order_release); }

    // ---- consumer (one thread) ----
    // Every record pushed so far, oldest first (a singly linked list through next; nullptr: none). Hand each one (or
    // the list) back with release() when done with it.
    InprocRecord* take() {
        InprocRecord* r = head_.exchange(nullptr, std::memory_order_acquire);
        InprocRecord* out = nullptr;
        while (r) { InprocRecord* n = r->next; r->next = out; out = r; r = n; }
        return out;
    }
    void release_one(InprocRecord* r) {
        queued_.fetch_sub(r->size, std::memory_order_relaxed);
        std::free(r);
    }
    void release(InprocRecord* r) { while (r) { InprocRecord* n = r->next; release_one(r); r = n; } }
    // Read closed() before take(): closed and nothing taken means every record has been seen.
    bool closed() const { return closed_.load(std::memory_order_acquire) != 0; }
    void request_key() { key_request_.store(1, std::memory_order_release); }
    std::uint64_t queued_bytes() const { return queued_.load(std::memory_order_relaxed); }

    std::atomic<std::uint64_t> polls{0}, records{0}, keyframes{0}, dropped{0};

private:
    void drop() { dropped.fetch_add(1, std::memory_order_relaxed); drop_pending_ = true; }

    const std::uint64_t cap_;
    std::atomic<InprocRecord*> head_{nullptr};
    std::atomic<std::uint64_t> queued_{0};
    std::atomic<std::uint32_t> closed_{0}, key_request_{0};
    bool sent_key_ = false, drop_pending_ = false;   // producer only
};

// The process's in-process feed (set by b3render.dll's b3r_open before the renderer starts; b3replay --inproc reads it).
inline Inproc*& inproc_instance() {
    static Inproc* p = nullptr;
    return p;
}

}  // namespace b3live

#endif
