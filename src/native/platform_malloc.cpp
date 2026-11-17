// The native backend's platform tier, newlib's allocator (boot1; src/native/platform.h).
//
// Retail links newlib's mallocr.c (Doug Lea's malloc 2.6.4) built for the EE [C decomp runtime/libc.s]: SIZE_SZ 4,
// MALLOC_ALIGNMENT 16 (request2size = (n + 0x13) & ~15, at least MINSIZE 16: _malloc_r 0x1235D0), 128 bins in
// __malloc_av_ 0x4876F8 (0x408 bytes: pairs of 4-byte pointers), small bins below 504 bytes, page size 0x1000, `long`
// 64-bit (so __malloc_top_pad, __malloc_trim_threshold, __malloc_max_sbrked_mem / max_total_mem are 8-byte and
// remainder sizes are signed 64-bit: newlib's long_sub_size_t), sbrked_mem = __malloc_current_mallinfo.arena (32-bit).
// Sony's recursive lock around it (__malloc_lock 0x124120: GetThreadId, __malloc_lock_owner, call_count, WaitSema)
// and libkernl's sbrk 0x10C520 (heap_ptr.39, bounded by EndOfHeap; ENOMEM through __errno = _impure_ptr->_errno).
//
// Everything works on guest memory at retail's addresses, so the heap (chunk headers, bins, binblocks, top, the last
// remainder) and every global are what retail leaves. Proof: tests/test_native_platform.cpp runs random sequences of
// malloc / free / realloc / calloc / memalign / malloc_trim against the retail functions in tests/r5900_mini.h and
// compares the whole heap region and the allocator's globals after every call.
#include "platform.h"

#include "b3host/call.h"

#include <cstdint>

namespace b3native::platform {
namespace {

using b3host::gaddr;
using b3host::CallArgs;
using b3host::CallResult;

// [C decomp config/symbol_addrs_b3names.txt]
constexpr gaddr kAv = 0x004876F8u;             // __malloc_av_
constexpr gaddr kTrimThreshold = 0x00487B00u;  // unsigned long (64-bit)
constexpr gaddr kTopPad = 0x00487B08u;         // unsigned long (64-bit)
constexpr gaddr kSbrkBase = 0x00487B10u;       // char *
constexpr gaddr kMaxSbrked = 0x00487B18u;      // unsigned long (64-bit)
constexpr gaddr kMaxTotal = 0x00487B20u;       // unsigned long (64-bit)
constexpr gaddr kMallinfo = 0x00487B28u;       // struct mallinfo; +0 arena = sbrked_mem
constexpr gaddr kLockOwner = 0x00487B50u;      // __malloc_lock_owner
constexpr gaddr kCallCount = 0x00487B54u;      // call_count
constexpr gaddr kHeapPtr = 0x00484154u;        // sbrk's heap_ptr.39
constexpr gaddr kErrnoGlobal = 0x01ECE340u;    // _sbrk_r's errno word (D_01ECE340)
constexpr gaddr kImpurePtr = 0x004876E8u;

constexpr std::uint32_t SIZE_SZ = 4, MINSIZE = 16, ALIGN_MASK = 15, PAGESZ = 0x1000, PREV_INUSE = 1;
constexpr std::uint32_t MAX_SMALLBIN_SIZE = 512, SMALLBIN_WIDTH = 8, BINBLOCKWIDTH = 4;

inline std::uint32_t ld(gaddr a) { return b3host::load<std::uint32_t>(a); }
inline void st(gaddr a, std::uint32_t v) { b3host::store<std::uint32_t>(a, v); }
inline std::uint64_t ld64(gaddr a) { return b3host::load<std::uint64_t>(a); }
inline void st64(gaddr a, std::uint64_t v) { b3host::store<std::uint64_t>(a, v); }

// chunk fields
inline std::uint32_t prev_size(gaddr p) { return ld(p); }
inline std::uint32_t head(gaddr p) { return ld(p + 4); }
inline void set_head(gaddr p, std::uint32_t s) { st(p + 4, s); }
inline std::uint32_t chunksize(gaddr p) { return head(p) & ~3u; }
inline gaddr fd(gaddr p) { return ld(p + 8); }
inline gaddr bk(gaddr p) { return ld(p + 12); }
inline void set_fd(gaddr p, gaddr v) { st(p + 8, v); }
inline void set_bk(gaddr p, gaddr v) { st(p + 12, v); }
inline void set_foot(gaddr p, std::uint32_t s) { st(p + s, s); }
inline void set_head_size(gaddr p, std::uint32_t s) { st(p + 4, (head(p) & PREV_INUSE) | s); }
inline bool inuse_bit_at_offset(gaddr p, std::uint32_t s) { return (head(p + s) & PREV_INUSE) != 0; }
inline void set_inuse_bit_at_offset(gaddr p, std::uint32_t s) { st(p + s + 4, head(p + s) | PREV_INUSE); }
inline gaddr chunk2mem(gaddr p) { return p + 2 * SIZE_SZ; }
inline gaddr mem2chunk(gaddr m) { return m - 2 * SIZE_SZ; }

// bins
inline gaddr bin_at(std::uint32_t i) { return kAv + 8 * i; }   // &av_[2i + 2] - 2 * SIZE_SZ
inline gaddr top() { return ld(kAv + 8); }                      // bin_at(0)->fd
inline void set_top(gaddr p) { st(kAv + 8, p); }
inline std::uint32_t binblocks() { return ld(kAv + 4); }        // bin_at(0)->size
inline void set_binblocks(std::uint32_t v) { st(kAv + 4, v); }
constexpr gaddr kLastRemainder = kAv + 8;                       // bin_at(1)
inline gaddr initial_top() { return kAv; }                      // bin_at(0)
inline std::uint32_t idx2binblock(std::uint32_t ix) { return 1u << (ix / BINBLOCKWIDTH); }
inline void mark_binblock(std::uint32_t ix) { set_binblocks(binblocks() | idx2binblock(ix)); }
inline bool is_small_request(std::uint32_t nb) { return nb < MAX_SMALLBIN_SIZE - SMALLBIN_WIDTH; }
inline std::uint32_t smallbin_index(std::uint32_t sz) { return sz >> 3; }
inline std::uint32_t bin_index(std::uint32_t sz) {
    const std::uint32_t s9 = sz >> 9;
    return s9 == 0 ? sz >> 3 : s9 <= 4 ? 56 + (sz >> 6) : s9 <= 20 ? 91 + (sz >> 9) : s9 <= 84 ? 110 + (sz >> 12)
         : s9 <= 340 ? 119 + (sz >> 15) : s9 <= 1364 ? 124 + (sz >> 18) : 126;
}
inline std::uint32_t request2size(std::uint32_t req) {
    return req + (SIZE_SZ + ALIGN_MASK) < MINSIZE + ALIGN_MASK ? MINSIZE : (req + (SIZE_SZ + ALIGN_MASK)) & ~ALIGN_MASK;
}
// newlib's long_sub_size_t: a signed 64-bit difference of two 32-bit sizes
inline std::int64_t long_sub(std::uint32_t x, std::uint32_t y) {
    return x < y ? -static_cast<std::int64_t>(y - x) : static_cast<std::int64_t>(x - y);
}

inline void unlink(gaddr p) {
    const gaddr b = bk(p), f = fd(p);
    set_bk(f, b);
    set_fd(b, f);
}
inline void link_last_remainder(gaddr p) {
    set_fd(kLastRemainder, p);
    set_bk(kLastRemainder, p);
    set_fd(p, kLastRemainder);
    set_bk(p, kLastRemainder);
}
inline void clear_last_remainder() {
    set_fd(kLastRemainder, kLastRemainder);
    set_bk(kLastRemainder, kLastRemainder);
}
void frontlink(gaddr p, std::uint32_t s) {
    gaddr b, f;
    if (s < MAX_SMALLBIN_SIZE) {
        const std::uint32_t idx = smallbin_index(s);
        mark_binblock(idx);
        b = bin_at(idx);
        f = fd(b);
    } else {
        const std::uint32_t idx = bin_index(s);
        b = bin_at(idx);
        f = fd(b);
        if (f == b) {
            mark_binblock(idx);
        } else {
            while (f != b && s < chunksize(f)) f = fd(f);
            b = bk(f);
        }
    }
    set_bk(p, b);
    set_fd(p, f);
    set_bk(f, p);
    set_fd(b, p);
}

// Sony's lock (single thread natively: the main thread's id is 1, the semaphore is never contended)
void lock() {
    if (ld(kLockOwner) != 1u) st(kLockOwner, 1u);   // WaitSema(__sce_sema_id) first in retail: no guest state
    st(kCallCount, ld(kCallCount) + 1);
}
void unlock() {
    const std::uint32_t c = ld(kCallCount) - 1;
    st(kCallCount, c);
    if (c == 0) st(kLockOwner, 0xFFFFFFFFu);        // then SignalSema
}

// libkernl sbrk(incr) [C 0x10C520]: Status.EIE is 0 natively (no di / ei); ENOMEM -> _impure_ptr->_errno = 12, -1
gaddr sbrk(std::int32_t incr) {
    const gaddr old = ld(kHeapPtr);
    const gaddr want = old + static_cast<gaddr>(incr);
    if (heap_end() < want) {
        st(ld(kImpurePtr), 12);
        return 0xFFFFFFFFu;
    }
    st(kHeapPtr, want);
    return old;
}
// _sbrk_r(reent, incr) [C 0x1265C8]: clears its errno word, then copies a nonzero one into reent->_errno on failure
gaddr sbrk_r(gaddr reent, std::int32_t incr) {
    st(kErrnoGlobal, 0);
    const gaddr r = sbrk(incr);
    if (r == 0xFFFFFFFFu && ld(kErrnoGlobal) != 0) st(reent, ld(kErrnoGlobal));
    return r;
}

gaddr malloc_r(gaddr reent, std::uint32_t bytes);
void free_r(gaddr reent, gaddr mem);

void update_max() {
    const std::uint64_t arena = static_cast<std::uint64_t>(static_cast<std::int64_t>(static_cast<std::int32_t>(ld(kMallinfo))));
    if (ld64(kMaxSbrked) < arena) st64(kMaxSbrked, arena);
    if (ld64(kMaxTotal) < arena) st64(kMaxTotal, arena);
}

// malloc_extend_top [C 0x123378]
void malloc_extend_top(gaddr reent, std::uint32_t nb) {
    const gaddr old_top = top();
    std::uint32_t old_top_size = chunksize(old_top);
    const gaddr old_end = old_top + old_top_size;
    std::uint32_t sbrk_size = static_cast<std::uint32_t>(nb + ld64(kTopPad) + MINSIZE);
    if (ld(kSbrkBase) != 0xFFFFFFFFu) sbrk_size = (sbrk_size + (PAGESZ - 1)) & ~(PAGESZ - 1);
    gaddr brk = sbrk_r(reent, static_cast<std::int32_t>(sbrk_size));
    if (brk == 0xFFFFFFFFu || (brk < old_end && old_top != initial_top())) return;
    st(kMallinfo, ld(kMallinfo) + sbrk_size);
    if (brk == old_end) {
        set_head(top(), (sbrk_size + old_top_size) | PREV_INUSE);
    } else {
        if (ld(kSbrkBase) == 0xFFFFFFFFu) st(kSbrkBase, brk);
        else st(kMallinfo, ld(kMallinfo) + (brk - old_end));
        std::uint32_t correction;
        const std::uint32_t front_misalign = chunk2mem(brk) & ALIGN_MASK;
        if (front_misalign > 0) {
            correction = (ALIGN_MASK + 1) - front_misalign;
            brk += correction;
        } else {
            correction = 0;
        }
        correction += PAGESZ - ((brk + sbrk_size) & (PAGESZ - 1));
        const gaddr new_brk = sbrk_r(reent, static_cast<std::int32_t>(correction));
        if (new_brk == 0xFFFFFFFFu) return;
        st(kMallinfo, ld(kMallinfo) + correction);
        set_top(brk);
        const std::uint32_t top_size = new_brk - brk + correction;
        set_head(brk, top_size | PREV_INUSE);
        if (old_top != initial_top()) {
            if (old_top_size < MINSIZE) {
                set_head(brk, PREV_INUSE);
                return;
            }
            old_top_size = (old_top_size - 3 * SIZE_SZ) & ~ALIGN_MASK;
            set_head_size(old_top, old_top_size);
            set_head(old_top + old_top_size, SIZE_SZ | PREV_INUSE);
            set_head(old_top + old_top_size + SIZE_SZ, SIZE_SZ | PREV_INUSE);
            if (old_top_size >= MINSIZE) free_r(reent, chunk2mem(old_top));
        }
    }
    update_max();
}

// _malloc_r [C 0x1235D0]
gaddr malloc_r(gaddr reent, std::uint32_t bytes) {
    const std::uint32_t nb = request2size(bytes);
    lock();
    std::uint32_t idx;
    if (is_small_request(nb)) {
        idx = smallbin_index(nb);
        const gaddr q = bin_at(idx);
        const gaddr victim = bk(q);
        if (victim != q) {
            const std::uint32_t victim_size = chunksize(victim);
            unlink(victim);
            set_inuse_bit_at_offset(victim, victim_size);
            unlock();
            return chunk2mem(victim);
        }
        idx += 2;
    } else {
        idx = bin_index(nb);
        const gaddr bin = bin_at(idx);
        bool rescan = false;
        for (gaddr victim = bk(bin); victim != bin; victim = bk(victim)) {
            const std::uint32_t victim_size = chunksize(victim);
            const std::int64_t remainder_size = long_sub(victim_size, nb);
            if (remainder_size >= static_cast<std::int64_t>(MINSIZE)) {
                --idx;
                rescan = true;
                break;
            } else if (remainder_size >= 0) {
                unlink(victim);
                set_inuse_bit_at_offset(victim, victim_size);
                unlock();
                return chunk2mem(victim);
            }
        }
        (void)rescan;
        ++idx;
    }
    // the last split-off remainder
    gaddr victim = fd(kLastRemainder);
    if (victim != kLastRemainder) {
        const std::uint32_t victim_size = chunksize(victim);
        const std::int64_t remainder_size = long_sub(victim_size, nb);
        if (remainder_size >= static_cast<std::int64_t>(MINSIZE)) {
            const gaddr remainder = victim + nb;
            set_head(victim, nb | PREV_INUSE);
            link_last_remainder(remainder);
            set_head(remainder, static_cast<std::uint32_t>(remainder_size) | PREV_INUSE);
            set_foot(remainder, static_cast<std::uint32_t>(remainder_size));
            unlock();
            return chunk2mem(victim);
        }
        clear_last_remainder();
        if (remainder_size >= 0) {
            set_inuse_bit_at_offset(victim, victim_size);
            unlock();
            return chunk2mem(victim);
        }
        frontlink(victim, victim_size);
    }
    // bins in blocks of four
    std::uint32_t block = idx2binblock(idx);
    if (block <= binblocks()) {
        if ((block & binblocks()) == 0) {
            idx = (idx & ~(BINBLOCKWIDTH - 1)) + BINBLOCKWIDTH;
            block <<= 1;
            while ((block & binblocks()) == 0) {
                idx += BINBLOCKWIDTH;
                block <<= 1;
            }
        }
        for (;;) {
            std::uint32_t startidx = idx;
            gaddr q = bin_at(idx), bin = q;
            do {
                for (victim = bk(bin); victim != bin; victim = bk(victim)) {
                    const std::uint32_t victim_size = chunksize(victim);
                    const std::int64_t remainder_size = long_sub(victim_size, nb);
                    if (remainder_size >= static_cast<std::int64_t>(MINSIZE)) {
                        const gaddr remainder = victim + nb;
                        set_head(victim, nb | PREV_INUSE);
                        unlink(victim);
                        link_last_remainder(remainder);
                        set_head(remainder, static_cast<std::uint32_t>(remainder_size) | PREV_INUSE);
                        set_foot(remainder, static_cast<std::uint32_t>(remainder_size));
                        unlock();
                        return chunk2mem(victim);
                    } else if (remainder_size >= 0) {
                        set_inuse_bit_at_offset(victim, victim_size);
                        unlink(victim);
                        unlock();
                        return chunk2mem(victim);
                    }
                }
                bin += 8;   // next_bin
                ++idx;
            } while ((idx & (BINBLOCKWIDTH - 1)) != 0);
            do {
                if ((startidx & (BINBLOCKWIDTH - 1)) == 0) {
                    set_binblocks(binblocks() & ~block);
                    break;
                }
                --startidx;
                q -= 8;     // prev_bin
            } while (fd(q) == q);
            block <<= 1;
            if (block <= binblocks() && block != 0) {
                while ((block & binblocks()) == 0) {
                    idx += BINBLOCKWIDTH;
                    block <<= 1;
                }
            } else {
                break;
            }
        }
    }
    // the top chunk
    std::int64_t remainder_size = long_sub(chunksize(top()), nb);
    if (chunksize(top()) < nb || remainder_size < static_cast<std::int64_t>(MINSIZE)) {
        malloc_extend_top(reent, nb);
        remainder_size = long_sub(chunksize(top()), nb);
        if (chunksize(top()) < nb || remainder_size < static_cast<std::int64_t>(MINSIZE)) {
            unlock();
            return 0;
        }
    }
    victim = top();
    set_head(victim, nb | PREV_INUSE);
    set_top(victim + nb);
    set_head(top(), static_cast<std::uint32_t>(remainder_size) | PREV_INUSE);
    unlock();
    return chunk2mem(victim);
}

// _malloc_trim_r [C 0x122180]
int malloc_trim_r(gaddr reent, std::uint32_t pad) {   // pad: size_t
    lock();
    const std::int64_t top_size = chunksize(top());
    const std::uint64_t num = static_cast<std::uint64_t>(top_size) - pad - MINSIZE + (PAGESZ - 1);
    const std::int64_t extra = static_cast<std::int64_t>((num / PAGESZ - 1) * PAGESZ);
    if (extra < static_cast<std::int64_t>(PAGESZ)) {
        unlock();
        return 0;
    }
    gaddr current_brk = sbrk_r(reent, 0);
    if (current_brk != top() + static_cast<gaddr>(top_size)) {
        unlock();
        return 0;
    }
    const gaddr new_brk = sbrk_r(reent, static_cast<std::int32_t>(-extra));
    if (new_brk == 0xFFFFFFFFu) {
        current_brk = sbrk_r(reent, 0);
        const std::int64_t ts = static_cast<std::int64_t>(current_brk) - static_cast<std::int64_t>(top());
        if (ts >= static_cast<std::int64_t>(MINSIZE)) {
            st(kMallinfo, current_brk - ld(kSbrkBase));
            set_head(top(), static_cast<std::uint32_t>(ts) | PREV_INUSE);
        }
        unlock();
        return 0;
    }
    set_head(top(), static_cast<std::uint32_t>(top_size - extra) | PREV_INUSE);
    st(kMallinfo, ld(kMallinfo) - static_cast<std::uint32_t>(extra));
    unlock();
    return 1;
}

// _free_r [C 0x121E88]
void free_r(gaddr reent, gaddr mem) {
    if (mem == 0) return;
    lock();
    gaddr p = mem2chunk(mem);
    const std::uint32_t hd = head(p);
    std::uint32_t sz = hd & ~PREV_INUSE;
    const gaddr next = p + sz;
    const std::uint32_t nextsz = chunksize(next);
    if (next == top()) {
        sz += nextsz;
        if (!(hd & PREV_INUSE)) {
            const std::uint32_t prevsz = prev_size(p);
            p -= prevsz;
            sz += prevsz;
            unlink(p);
        }
        set_head(p, sz | PREV_INUSE);
        set_top(p);
        if (static_cast<std::uint64_t>(sz) >= ld64(kTrimThreshold)) malloc_trim_r(reent, static_cast<std::uint32_t>(ld64(kTopPad)));
        unlock();
        return;
    }
    set_head(next, nextsz);
    bool islr = false;
    if (!(hd & PREV_INUSE)) {
        const std::uint32_t prevsz = prev_size(p);
        p -= prevsz;
        sz += prevsz;
        if (fd(p) == kLastRemainder) islr = true;
        else unlink(p);
    }
    if (!inuse_bit_at_offset(next, nextsz)) {
        sz += nextsz;
        if (!islr && fd(next) == kLastRemainder) {
            islr = true;
            link_last_remainder(p);
        } else {
            unlink(next);
        }
    }
    set_head(p, sz | PREV_INUSE);
    set_foot(p, sz);
    if (!islr) frontlink(p, sz);
    unlock();
}

void copy(gaddr d, gaddr s, std::uint32_t n) {   // MALLOC_COPY: memcpy forward (d < s whenever they overlap)
    for (std::uint32_t k = 0; k < n; k += 4) st(d + k, ld(s + k));
}

// _realloc_r [C 0x125E70]
gaddr realloc_r(gaddr reent, gaddr oldmem, std::uint32_t bytes) {
    if (oldmem == 0) return malloc_r(reent, bytes);
    lock();
    const gaddr oldp = mem2chunk(oldmem);
    gaddr newp = oldp;
    const std::uint32_t oldsize = chunksize(oldp);
    std::uint32_t newsize = oldsize;
    const std::uint32_t nb = request2size(bytes);
    if (static_cast<std::int64_t>(oldsize) < static_cast<std::int64_t>(nb)) {
        gaddr next = oldp + oldsize;
        std::uint32_t nextsize = 0;
        auto as_long = [](std::uint32_t v) { return static_cast<std::int64_t>(v); };   // (long)(INTERNAL_SIZE_T): zero-extended
        if (next == top() || !inuse_bit_at_offset(next, chunksize(next))) {
            nextsize = chunksize(next);
            if (next == top()) {
                if (as_long(nextsize + newsize) >= as_long(nb + MINSIZE)) {
                    newsize += nextsize;
                    set_top(oldp + nb);
                    set_head(top(), (newsize - nb) | PREV_INUSE);
                    set_head_size(oldp, nb);
                    unlock();
                    return chunk2mem(oldp);
                }
            } else if (as_long(nextsize + newsize) >= as_long(nb)) {
                unlink(next);
                newsize += nextsize;
                goto split;
            }
        } else {
            next = 0;
            nextsize = 0;
        }
        if (!(head(oldp) & PREV_INUSE)) {
            const gaddr prev = oldp - prev_size(oldp);
            const std::uint32_t prevsize = chunksize(prev);
            if (next != 0) {
                if (next == top()) {
                    if (as_long(nextsize + prevsize + newsize) >= as_long(nb + MINSIZE)) {
                        unlink(prev);
                        newp = prev;
                        newsize += prevsize + nextsize;
                        copy(chunk2mem(newp), oldmem, oldsize - SIZE_SZ);
                        set_top(newp + nb);
                        set_head(top(), (newsize - nb) | PREV_INUSE);
                        set_head_size(newp, nb);
                        unlock();
                        return chunk2mem(newp);
                    }
                } else if (as_long(nextsize + prevsize + newsize) >= as_long(nb)) {
                    unlink(next);
                    unlink(prev);
                    newp = prev;
                    newsize += nextsize + prevsize;
                    copy(chunk2mem(newp), oldmem, oldsize - SIZE_SZ);
                    goto split;
                }
            }
            if (prev != 0 && as_long(prevsize + newsize) >= as_long(nb)) {
                unlink(prev);
                newp = prev;
                newsize += prevsize;
                copy(chunk2mem(newp), oldmem, oldsize - SIZE_SZ);
                goto split;
            }
        }
        {
            const gaddr newmem = malloc_r(reent, bytes);
            if (newmem == 0) {
                unlock();
                return 0;
            }
            newp = mem2chunk(newmem);
            if (newp == oldp + chunksize(oldp)) {
                newsize += chunksize(newp);
                newp = oldp;
                goto split;
            }
            copy(newmem, oldmem, oldsize - SIZE_SZ);
            free_r(reent, oldmem);
            unlock();
            return newmem;
        }
    }
split: {
    const std::int64_t remainder_size = long_sub(newsize, nb);
    if (remainder_size >= static_cast<std::int64_t>(MINSIZE)) {
        const gaddr remainder = newp + nb;
        set_head_size(newp, nb);
        set_head(remainder, static_cast<std::uint32_t>(remainder_size) | PREV_INUSE);
        set_inuse_bit_at_offset(remainder, static_cast<std::uint32_t>(remainder_size));
        free_r(reent, chunk2mem(remainder));
    } else {
        set_head_size(newp, newsize);
        set_inuse_bit_at_offset(newp, newsize);
    }
    unlock();
    return chunk2mem(newp);
}
}

// _calloc_r [C 0x11FE00]: _malloc_r(n * size), then zero chunksize - SIZE_SZ bytes
gaddr calloc_r(gaddr reent, std::uint32_t n, std::uint32_t size) {
    const gaddr mem = malloc_r(reent, n * size);
    if (mem == 0) return 0;
    const std::uint32_t csz = chunksize(mem2chunk(mem)) - SIZE_SZ;
    b3host::fill_guest(mem, 0, csz);
    return mem;
}

// _memalign_r [C 0x123110]
gaddr memalign_r(gaddr reent, std::uint32_t alignment, std::uint32_t bytes) {
    if (alignment <= ALIGN_MASK + 1) return malloc_r(reent, bytes);
    if (alignment < MINSIZE) alignment = MINSIZE;
    const std::uint32_t nb = request2size(bytes);
    const gaddr m = malloc_r(reent, nb + alignment + MINSIZE);
    if (m == 0) return 0;
    lock();
    gaddr p = mem2chunk(m);
    if (m % alignment != 0) {
        gaddr brk = mem2chunk((m + alignment - 1) & (0u - alignment));
        if (static_cast<std::int64_t>(brk) - static_cast<std::int64_t>(p) < static_cast<std::int64_t>(MINSIZE)) brk += alignment;
        const gaddr newp = brk;
        const std::uint32_t leadsize = brk - p;
        const std::uint32_t newsize = chunksize(p) - leadsize;
        set_head(newp, newsize | PREV_INUSE);
        set_inuse_bit_at_offset(newp, newsize);
        set_head_size(p, leadsize);
        free_r(reent, chunk2mem(p));
        p = newp;
    }
    const std::int64_t remainder_size = long_sub(chunksize(p), nb);
    if (remainder_size >= static_cast<std::int64_t>(MINSIZE)) {
        const gaddr remainder = p + nb;
        set_head(remainder, static_cast<std::uint32_t>(remainder_size) | PREV_INUSE);
        set_head_size(p, nb);
        free_r(reent, chunk2mem(remainder));
    }
    unlock();
    return chunk2mem(p);
}

inline std::uint32_t a32(const CallArgs& a, unsigned i) { return a.gpr[i].w[0]; }
inline void ret(CallResult& r, std::uint32_t v) {
    r = CallResult{};
    r.v0 = b3host::detail::gpr64(b3host::detail::sext32(v));
}
inline gaddr impure() { return ld(kImpurePtr); }

}  // namespace

// EndOfHeap (syscall 0x3E): the heap ends at the main thread's initial stack pointer (docs/specs/ee-kernel.md
// §9: SetupHeap size -1 -> stack top - 0x2A0 = 0x01FFFD60 for crt0's stack 0x1FF0000 + 0x10000)
gaddr heap_end() { return 0x01FFFD60u; }

// boot2: _malloc_r on the guest's reent for platform code (newlib's stdout buffer, platform_printf.cpp)
gaddr platform_malloc_r(std::uint32_t bytes) { lock(); const gaddr m = malloc_r(impure(), bytes); unlock(); return m; }
void malloc_(const CallArgs& a, CallResult& r) { lock(); const gaddr m = malloc_r(impure(), a32(a, 0)); unlock(); ret(r, m); }
void free_(const CallArgs& a, CallResult& r) { lock(); free_r(impure(), a32(a, 0)); unlock(); r = CallResult{}; }
void realloc_(const CallArgs& a, CallResult& r) { lock(); const gaddr m = realloc_r(impure(), a32(a, 0), a32(a, 1)); unlock(); ret(r, m); }
void calloc_(const CallArgs& a, CallResult& r) { lock(); const gaddr m = calloc_r(impure(), a32(a, 0), a32(a, 1)); unlock(); ret(r, m); }
void memalign_(const CallArgs& a, CallResult& r) { lock(); const gaddr m = memalign_r(impure(), a32(a, 0), a32(a, 1)); unlock(); ret(r, m); }
void malloc_r_(const CallArgs& a, CallResult& r) { ret(r, malloc_r(a32(a, 0), a32(a, 1))); }
void free_r_(const CallArgs& a, CallResult& r) { free_r(a32(a, 0), a32(a, 1)); r = CallResult{}; }
void realloc_r_(const CallArgs& a, CallResult& r) { ret(r, realloc_r(a32(a, 0), a32(a, 1), a32(a, 2))); }
void calloc_r_(const CallArgs& a, CallResult& r) { ret(r, calloc_r(a32(a, 0), a32(a, 1), a32(a, 2))); }
void memalign_r_(const CallArgs& a, CallResult& r) { ret(r, memalign_r(a32(a, 0), a32(a, 1), a32(a, 2))); }
void malloc_trim_r_(const CallArgs& a, CallResult& r) { ret(r, static_cast<std::uint32_t>(malloc_trim_r(a32(a, 0), a32(a, 1)))); }
void sbrk_(const CallArgs& a, CallResult& r) { ret(r, sbrk(static_cast<std::int32_t>(a32(a, 0)))); }
void sbrk_r_(const CallArgs& a, CallResult& r) { ret(r, sbrk_r(a32(a, 0), static_cast<std::int32_t>(a32(a, 1)))); }
void malloc_lock_(const CallArgs&, CallResult& r) { lock(); r = CallResult{}; }
void malloc_unlock_(const CallArgs&, CallResult& r) { unlock(); r = CallResult{}; }
void end_of_heap(const CallArgs&, CallResult& r) { ret(r, heap_end()); }
void errno_(const CallArgs&, CallResult& r) { ret(r, impure()); }
void malloc_extend_top_(const CallArgs& a, CallResult& r) { malloc_extend_top(a32(a, 0), a32(a, 1)); r = CallResult{}; }

}  // namespace b3native::platform
